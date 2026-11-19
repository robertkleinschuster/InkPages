#include "ink/InkLayer.h"

#include <algorithm>
#include <bit>
#include <format>

#include <winrt/Windows.Devices.Input.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.Core.h>
#include <winrt/Windows.UI.Input.h>

#include "tools/ExactEraser.h"
#include "ink/StrokeEdit.h"
#include "inkpages/Log.h"

namespace inkpages::detail
{
    using namespace winrt::Windows::UI::Input::Inking;
    using winrt::Windows::Devices::Input::PointerDeviceType;
    using winrt::Windows::UI::Core::CoreCursor;
    using winrt::Windows::UI::Core::CoreCursorType;
    using winrt::Windows::UI::Core::CoreInputDeviceTypes;
    using winrt::Windows::UI::Core::PointerEventArgs;

    namespace
    {
        struct WorkItem : winrt::implements<WorkItem, IInkHostWorkItem>
        {
            WorkItem(char const* name, std::function<void()> work) : name(name), work(std::move(work)) {}
            HRESULT STDMETHODCALLTYPE Invoke() noexcept override
            {
                try
                {
                    work();
                }
                catch (...)
                {
                    LogException(std::format("ink thread: {}", name));
                }
                return S_OK;
            }
            char const* name;
            std::function<void()> work;
        };

        // The presenter asks the app to commit DirectComposition: the device belongs to the UI thread.
        struct CommitHandler : winrt::implements<CommitHandler, IInkCommitRequestHandler>
        {
            CommitHandler(HWND window, UINT message) : window(window), message(message) {}
            HRESULT STDMETHODCALLTYPE OnCommitRequested() noexcept override
            {
                PostMessageW(window, message, 0, 0);
                return S_OK;
            }
            HWND window;
            UINT message;
        };

        WPARAM PackPoint(PointF p)
        {
            return (static_cast<WPARAM>(std::bit_cast<uint32_t>(p.x)) << 32) | std::bit_cast<uint32_t>(p.y);
        }

        bool IsPen(winrt::Windows::UI::Input::PointerPoint const& p)
        {
            return p.PointerDevice().PointerDeviceType() == PointerDeviceType::Pen;
        }

        // The pen turned over: inverted while hovering, inverted and/or eraser while touching.
        bool IsEraserEnd(winrt::Windows::UI::Input::PointerPoint const& p)
        {
            return IsPen(p) && (p.Properties().IsInverted() || p.Properties().IsEraser());
        }

        // A pointer event's points, oldest first (the docs give no order for GetIntermediatePoints: sorted by time).
        std::vector<PointF> Path(PointerEventArgs const& e)
        {
            std::vector<std::pair<uint64_t, PointF>> timed;
            for (auto const& p : e.GetIntermediatePoints())
            {
                timed.push_back({ p.Timestamp(), { p.Position().X, p.Position().Y } });
            }
            std::sort(timed.begin(), timed.end(), [](auto const& a, auto const& b) { return a.first < b.first; });
            std::vector<PointF> path;
            for (auto const& t : timed) path.push_back(t.second);
            if (path.empty()) path.push_back({ e.CurrentPoint().Position().X, e.CurrentPoint().Position().Y });
            return path;
        }
    }

    PointF InkLayer::UnpackPoint(WPARAM wp)
    {
        return { std::bit_cast<float>(static_cast<uint32_t>(wp >> 32)), std::bit_cast<float>(static_cast<uint32_t>(wp)) };
    }

    InkLayer::InkLayer(HWND window, InkMessages messages, IDCompositionVisual* root, bool mouseInks)
        : window(window), messages(messages)
    {
        host = winrt::create_instance<IInkDesktopHost>(__uuidof(InkDesktopHost), CLSCTX_INPROC_SERVER);
        winrt::com_ptr<IDCompositionVisual> visual;
        visual.copy_from(root);
        OnInkThread("create presenter", [this, visual, mouseInks] { Create(visual.get(), mouseInks); });
    }

    InkLayer::~InkLayer()
    {
        // Release the presenter's objects on their own thread, then the host (which ends that thread).
        HANDLE done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        OnInkThread("shutdown", [this, done] {
            history.Clear();
            eraser.reset();
            ink.container = nullptr;
            input = nullptr;
            presenter = nullptr;
            desktop = nullptr;
            SetEvent(done);
        });
        if (WaitForSingleObject(done, 3000) != WAIT_OBJECT_0) LogError("ink: shutdown did not finish within 3 s");
        CloseHandle(done);
        host = nullptr;
    }

    void InkLayer::OnInkThread(char const* name, std::function<void()> work)
    {
        if (hostThread != 0 && GetCurrentThreadId() == hostThread)
        {
            WorkItem(name, std::move(work)).Invoke(); // already there: in order, at once
            return;
        }
        HRESULT hr = host->QueueWorkItem(winrt::make<WorkItem>(name, std::move(work)).get());
        if (FAILED(hr)) LogHr(std::format("QueueWorkItem({})", name), hr);
    }

    void InkLayer::Create(IDCompositionVisual* root, bool mouseInks)
    {
        hostThread = GetCurrentThreadId();
        LARGE_INTEGER frequency, start, end;
        QueryPerformanceFrequency(&frequency);
        QueryPerformanceCounter(&start);
        winrt::check_hresult(LogHr("CreateAndInitializeInkPresenter", host->CreateAndInitializeInkPresenter(root, 1, 1,
            __uuidof(IInkPresenterDesktop), desktop.put_void())));
        winrt::check_hresult(desktop->SetCommitRequestHandler(winrt::make<CommitHandler>(window, messages.commit).get()));
        presenter = desktop.as<InkPresenter>();
        presenter.InputDeviceTypes(mouseInks ? CoreInputDeviceTypes::Pen | CoreInputDeviceTypes::Mouse : CoreInputDeviceTypes::Pen);
        ink.container = presenter.StrokeContainer();
        eraser = std::make_unique<PartialEraser>(ink);
        input = Core::CoreInkIndependentInputSource::Create(presenter);
        Subscribe();
        ApplyAttributes();
        ApplyMode();
        QueryPerformanceCounter(&end);
        Log(std::format("ink: presenter created in {:.1f} ms", (end.QuadPart - start.QuadPart) * 1000.0 / frequency.QuadPart));
        PostMessageW(window, messages.ready, 0, 0);
    }

    void InkLayer::Subscribe()
    {
        presenter.StrokesCollected([this](InkPresenter const&, InkStrokesCollectedEventArgs const& e) {
            try
            {
                auto edit = std::make_shared<StrokeEdit>(ink);
                for (InkStroke const& stroke : e.Strokes())
                {
                    int page = ink.PageFor(stroke);
                    ink.owner[stroke.Id()] = page;
                    edit->Added(stroke, page);
                    if (collected) collected(page, stroke);
                }
                Push(edit);
            }
            catch (...)
            {
                LogException("ink: strokes collected");
            }
        });
        // Windows ink's own eraser (whole strokes): the eraser tool with whole-stroke erasing, or the pen's eraser end.
        presenter.StrokesErased([this](InkPresenter const&, InkStrokesErasedEventArgs const& e) {
            try
            {
                auto edit = std::make_shared<StrokeEdit>(ink);
                for (InkStroke const& stroke : e.Strokes())
                {
                    edit->Removed(stroke, ink.PageOf(stroke));
                    ink.owner.erase(stroke.Id());
                }
                Log(std::format("ink: {} strokes erased (whole)", e.Strokes().Size()));
                Push(edit);
            }
            catch (...)
            {
                LogException("ink: strokes erased");
            }
        });

        // Unprocessed input (mode None): the exact eraser.
        auto events = presenter.UnprocessedInput();
        events.PointerPressed([this](InkUnprocessedInput const&, PointerEventArgs const& e) {
            try
            {
                auto p = e.CurrentPoint().Position();
                PointF at{ p.X, p.Y };
                OnInkThread("erase begin", [this, at] {
                    eraser->radius = tool.eraserRadius;
                    eraser->Begin(at);
                });
            }
            catch (...)
            {
                LogException("ink: unprocessed pressed");
            }
        });
        events.PointerMoved([this](InkUnprocessedInput const&, PointerEventArgs const& e) {
            try
            {
                auto path = Path(e);
                OnInkThread("erase move", [this, path] { eraser->MoveAlong(path); });
            }
            catch (...)
            {
                LogException("ink: unprocessed moved");
            }
        });
        auto end = [this](char const* what) {
            OnInkThread("erase end", [this, what] {
                if (!eraser->Running()) return;
                auto edit = eraser->End();
                Log(std::format("ink: exact erase {} ({} pages)", what, edit ? edit->Pages().size() : 0));
                Push(edit);
            });
        };
        events.PointerReleased([end](InkUnprocessedInput const&, PointerEventArgs const&) { end("ended"); });
        events.PointerLost([end](InkUnprocessedInput const&, PointerEventArgs const&) { end("lost"); });

        // The presenter's input (Windows' input thread): its first pointer event says the input is active - only then
        // the pen cursor is set (see ApplyCursor). Hover: the pen's eraser end is taken before it touches, the exact
        // eraser's circle follows the pen.
        auto hover = [this](PointerEventArgs const& e, bool leaving) {
            auto point = e.CurrentPoint();
            if (!cursorActive.exchange(true))
            {
                Log("ink: the presenter's input is active (first pointer event)");
                OnInkThread("cursor", [this] { ApplyCursor(); });
            }
            bool pen = IsPen(point);
            bool inverted = IsEraserEnd(point);
            auto pos = point.Position();
            bool shown = !leaving && (partialTool.load() || (inverted && partialEnd.load()));
            Circle({ pos.X, pos.Y }, shown);
            if (pen && !leaving && !point.IsInContact())
            {
                OnInkThread("pen end", [this, inverted] {
                    bool take = inverted && !tool.wholeStrokeErasing && tool.tool != Tool::Eraser;
                    if (take != eraserEndTaken)
                    {
                        eraserEndTaken = take;
                        Log(std::format("ink: the pen's eraser end {}", take ? "taken (exact eraser)" : "given back"));
                        ApplyMode(); // only the mode: a hover survives that (WinUI fact)
                    }
                });
            }
        };
        input.PointerEntering([hover](auto&&, PointerEventArgs const& e) {
            try { hover(e, false); } catch (...) { LogException("ink: pointer entering"); }
        });
        input.PointerHovering([hover](auto&&, PointerEventArgs const& e) {
            try { hover(e, false); } catch (...) { LogException("ink: pointer hovering"); }
        });
        input.PointerMoving([hover](auto&&, PointerEventArgs const& e) {
            try { hover(e, false); } catch (...) { LogException("ink: pointer moving"); }
        });
        input.PointerExiting([hover](auto&&, PointerEventArgs const& e) {
            try { hover(e, true); } catch (...) { LogException("ink: pointer exiting"); }
        });
    }

    void InkLayer::Circle(PointF at, bool shown)
    {
        PostMessageW(window, messages.circle, PackPoint(at), shown ? 1 : 0);
    }

    void InkLayer::SetLayout(PageLayout layout)
    {
        OnInkThread("layout", [this, layout] {
            ink.layout = layout;
            if (desktop) LogHr("IInkPresenterDesktop::SetSize", desktop->SetSize(layout.DocumentSize().width, layout.DocumentSize().height));
            PostMessageW(window, messages.commit, 0, 0);
        });
    }

    void InkLayer::SetTool(ToolState state)
    {
        partialTool = state.tool == Tool::Eraser && !state.wholeStrokeErasing;
        partialEnd = !state.wholeStrokeErasing;
        cursor = state.cursor;
        OnInkThread("tool", [this, state] {
            bool attributes = state.tool != tool.tool || !(state.color == tool.color) || state.width != tool.width;
            tool = state;
            eraser->radius = state.eraserRadius;
            if (state.tool == Tool::Eraser || state.wholeStrokeErasing) eraserEndTaken = false;
            if (presenter)
            {
                if (attributes) ApplyAttributes();
                ApplyMode();
                if (cursorActive) ApplyCursor();
            }
        });
    }

    void InkLayer::ApplyAttributes()
    {
        if (tool.tool == Tool::Eraser) return; // the pens keep theirs
        InkDrawingAttributes a;
        a.Color({ tool.color.a, tool.color.r, tool.color.g, tool.color.b });
        a.PenTip(PenTipShape::Circle);
        a.FitToCurve(false); // polylines: few-point shapes stay angular
        if (tool.tool == Tool::Highlighter)
        {
            a.Size({ tool.width, tool.width });
            a.IgnorePressure(true);
            a.DrawAsHighlighter(true);
        }
        else
        {
            float size = tool.width / InkWidth::FullPressureFactor; // the width is Xournal's: at full pressure
            a.Size({ size, size });
            a.IgnorePressure(false);
        }
        presenter.UpdateDefaultDrawingAttributes(a);
    }

    void InkLayer::ApplyMode()
    {
        InkInputProcessingMode mode = InkInputProcessingMode::Inking;
        if (tool.tool == Tool::Eraser) mode = tool.wholeStrokeErasing ? InkInputProcessingMode::Erasing : InkInputProcessingMode::None;
        else if (eraserEndTaken) mode = InkInputProcessingMode::None;
        auto config = presenter.InputProcessingConfiguration();
        if (config.Mode() != mode) config.Mode(mode);
    }

    // Measured (2026-11-19): setting PointerCursor while Windows still activates the presenter's input context kills the
    // process (fail-fast 0xC0000409 in Windows.UI.Input.Inking: CInputContext::_RunOnInkThreadSync E_UNEXPECTED, the
    // internal ink thread in CInputContext::Activate). The presenter's first pointer event shows its input is active.
    void InkLayer::ApplyCursor()
    {
        if (!input || !cursorActive) return;
        UINT id = cursor;
        input.PointerCursor(id ? CoreCursor(CoreCursorType::Custom, id) : nullptr);
        Log(std::format("ink: pen cursor {}", id));
    }

    void InkLayer::Push(std::shared_ptr<Edit> edit)
    {
        history.Push(std::move(edit));
        {
            std::scoped_lock guard(historyLock);
            historyState = { history.CanUndo(), history.CanRedo(), history.AtSavePoint(), history.Pages() };
        }
        PostMessageW(window, messages.history, 0, 0);
    }

    void InkLayer::Undo()
    {
        OnInkThread("undo", [this] {
            if (eraser->Running()) return; // not while a drag erases
            if (history.Undo()) Log("ink: undo");
            Push(nullptr);
        });
    }

    void InkLayer::Redo()
    {
        OnInkThread("redo", [this] {
            if (eraser->Running()) return;
            if (history.Redo()) Log("ink: redo");
            Push(nullptr);
        });
    }

    void InkLayer::MarkSaved()
    {
        OnInkThread("saved", [this] {
            history.MarkSaved();
            Push(nullptr);
        });
    }

    HistoryState InkLayer::History() const
    {
        std::scoped_lock guard(historyLock);
        return historyState;
    }

    void InkLayer::SetLivePages(std::set<int> const& pages)
    {
        OnInkThread("live pages", [this, pages] {
            if (!presenter) return;
            for (int page : std::set<int>(live))
            {
                if (!pages.contains(page)) Release(page);
            }
            for (int page : pages)
            {
                if (!live.contains(page)) Restore(page);
            }
        });
    }

    void InkLayer::Release(int page)
    {
        if (history.Pages().contains(page)) return; // its history needs its ink (the surface holds it anyway)
        RectF r = ink.layout.Page(page);
        std::vector<InkStroke> mine, strokes;
        for (InkStroke const& stroke : ink.container.GetStrokes())
        {
            if (ink.PageOf(stroke) == page)
            {
                mine.push_back(stroke);
                strokes.push_back(Translated(stroke, -r.left, -r.top));
            }
        }
        ink.Remove(mine);
        live.erase(page);
        Log(std::format("ink: page {} released ({} strokes)", page, strokes.size()));
        if (store) store(page, std::move(strokes));
    }

    void InkLayer::Restore(int page)
    {
        live.insert(page);
        if (!load) return;
        RectF r = ink.layout.Page(page);
        std::vector<InkStroke> strokes = load(page);
        for (InkStroke const& stroke : strokes) ink.Add(Translated(stroke, r.left, r.top), page);
        if (!strokes.empty()) Log(std::format("ink: page {} restored ({} strokes)", page, strokes.size()));
    }
}
