#include "ink/InkLayer.h"

#include <format>

#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.Core.h>

#include "ink/StrokeEdit.h"
#include "inkpages/model/PaperAppearance.h"
#include "inkpages/Log.h"

namespace inkpages::detail
{
    using namespace winrt::Windows::UI::Input::Inking;
    using winrt::Windows::UI::Core::CoreCursor;
    using winrt::Windows::UI::Core::CoreCursorType;
    using winrt::Windows::UI::Core::CoreInputDeviceTypes;
    using winrt::Windows::UI::Core::PointerEventArgs;

    namespace
    {
        uint32_t Pack(Color color)
        {
            return (uint32_t(color.a) << 24) | (uint32_t(color.r) << 16) | (uint32_t(color.g) << 8) | color.b;
        }
    }

    InkLayer::InkLayer(InkHost& host, std::function<void()> ready,
        std::function<void()> commit)
        : host(host), ready(std::move(ready)), commit(std::move(commit))
    {}

    void InkLayer::Start(IDCompositionVisual* root)
    {
        winrt::com_ptr<IDCompositionVisual> visual;
        visual.copy_from(root);
        winrt::check_hresult(host.Post("create presenter", [this, visual] { Create(visual.get()); }));
    }

    InkLayer::~InkLayer()
    {
        {
            std::scoped_lock lock(cursorInput->mutex);
            cursorInput->closed = true;
        }
        // The presenter's objects are released on their own thread - waited for without a timeout: the work refers to
        // this layer, which must not go before it ran.
        try
        {
            host.Run("release presenter", [this] {
                UnsubscribeStrokes();
                if (pointerEntering) input.PointerEntering(*pointerEntering);
                history.Clear();
                ink.container = nullptr;
                input = nullptr;
                presenter = nullptr;
                desktop = nullptr;
            }, INFINITE);
        }
        catch (...)
        {
            LogException("ink: release presenter");
        }
    }

    void InkLayer::Create(IDCompositionVisual* root)
    {
        LARGE_INTEGER frequency, start, end;
        QueryPerformanceFrequency(&frequency);
        QueryPerformanceCounter(&start);
        winrt::check_hresult(LogHr("CreateAndInitializeInkPresenter", host.Get()->CreateAndInitializeInkPresenter(root, 1, 1,
            __uuidof(IInkPresenterDesktop), desktop.put_void())));
        winrt::check_hresult(desktop->SetCommitRequestHandler(MakeCommitHandler(commit).get()));
        presenter = desktop.as<InkPresenter>();
        ApplyDevices();
        // The barrel button (and the right mouse button) is left to the app: it lassos with any tool.
        presenter.InputProcessingConfiguration().RightDragAction(InkInputRightDragAction::LeaveUnprocessed);
        ink.container = presenter.StrokeContainer();
        input = Core::CoreInkIndependentInputSource::Create(presenter);
        Subscribe();
        SetPen(pen);
        QueryPerformanceCounter(&end);
        Log(std::format("ink: presenter created in {:.1f} ms", (end.QuadPart - start.QuadPart) * 1000.0 / frequency.QuadPart));
        ready();
    }

    void InkLayer::PenColors::Start(uint32_t pointer)
    {
        std::scoped_lock lock(mutex);
        pending[pointer].push_back(applied.load());
    }

    void InkLayer::PenColors::Cancel(uint32_t pointer)
    {
        std::scoped_lock lock(mutex);
        auto it = pending.find(pointer);
        if (it == pending.end()) return;
        it->second.pop_back();
        if (it->second.empty()) pending.erase(it);
    }

    uint64_t InkLayer::PenColors::Take(uint32_t pointer)
    {
        std::scoped_lock lock(mutex);
        auto it = pending.find(pointer);
        if (it == pending.end()) return 0;
        auto colors = it->second.front();
        it->second.pop_front();
        if (it->second.empty()) pending.erase(it);
        return colors;
    }

    void InkLayer::Subscribe()
    {
        auto strokeInput = presenter.StrokeInput();
        strokeStarted = strokeInput.StrokeStarted([colors = penColors](auto&&, PointerEventArgs const& e) {
            try
            {
                colors->Start(e.CurrentPoint().PointerId());
            }
            catch (...)
            {
                LogException("ink: recording the starting pen color");
            }
        });
        strokeCanceled = strokeInput.StrokeCanceled([colors = penColors](auto&&, PointerEventArgs const& e) {
            try
            {
                colors->Cancel(e.CurrentPoint().PointerId());
            }
            catch (...)
            {
                LogException("ink: discarding a canceled pen color");
            }
        });
        strokesCollected = presenter.StrokesCollected([this](InkPresenter const&, InkStrokesCollectedEventArgs const& e) {
            try
            {
                auto edit = std::make_shared<StrokeEdit>(ink);
                std::vector<Placed> placed;
                for (InkStroke const& stroke : e.Strokes())
                {
                    auto sampledColors = penColors->Take(stroke.PointerId());
                    if (drop && drop(stroke))
                    {
                        ink.Remove({ stroke });
                        continue;
                    }
                    if (sampledColors)
                    {
                        auto actual = stroke.DrawingAttributes().Color();
                        constexpr uint32_t rgb = 0xffffff;
                        if ((Pack({ actual.A, actual.R, actual.G, actual.B }) & rgb) == (sampledColors & rgb))
                        {
                            uint32_t original = static_cast<uint32_t>(sampledColors >> 32);
                            ink.RememberOriginalColor(stroke, { actual.A, static_cast<uint8_t>(original >> 16),
                                static_cast<uint8_t>(original >> 8), static_cast<uint8_t>(original) });
                        }
                    }
                    auto pieces = ink.Split(stroke);
                    if (pieces.size() != 1 || pieces[0].stroke != stroke)
                    {
                        ink.Remove({ stroke }); // it crossed a page's edge: its pieces take its place
                        for (Placed const& p : pieces) ink.container.AddStroke(p.stroke);
                    }
                    for (Placed const& p : pieces)
                    {
                        ink.Adopt(p.stroke, p.page);
                        edit->Added(p.stroke, p.page);
                        placed.push_back(p);
                    }
                }
                history.Push(edit);
                if (collected && !placed.empty()) collected(placed);
            }
            catch (...)
            {
                LogException("ink: strokes collected");
            }
        });
        // Windows ink's own eraser (whole strokes): the eraser tool erasing whole strokes, or the pen's eraser end.
        strokesErased = presenter.StrokesErased([this](InkPresenter const&, InkStrokesErasedEventArgs const& e) {
            try
            {
                auto edit = std::make_shared<StrokeEdit>(ink);
                for (InkStroke const& stroke : e.Strokes())
                {
                    edit->Removed(stroke, ink.PageOf(stroke));
                    ink.Forget(stroke);
                }
                Log(std::format("ink: {} strokes erased (whole)", e.Strokes().Size()));
                history.Push(edit);
            }
            catch (...)
            {
                LogException("ink: strokes erased");
            }
        });
        // Measured (2026-11-19): setting PointerCursor while Windows still activates the presenter's input context kills
        // the process (fail-fast 0xC0000409, CInputContext::_RunOnInkThreadSync E_UNEXPECTED). The presenter's first
        // pointer event shows its input is active.
        pointerEntering = input.PointerEntering([this, state = cursorInput](auto&&, PointerEventArgs const&) {
            try
            {
                if (state->active) return;
                std::scoped_lock lock(state->mutex);
                if (state->closed || state->active.exchange(true)) return;
                Log("ink: the presenter's input is active (first pointer event)");
                host.Post("cursor", [this] { ApplyCursor(); });
            }
            catch (...)
            {
                LogException("ink: pointer entering");
            }
        });
    }

    void InkLayer::UnsubscribeStrokes()
    {
        if (strokesCollected) presenter.StrokesCollected(*strokesCollected);
        if (strokesErased) presenter.StrokesErased(*strokesErased);
        if (strokeStarted) presenter.StrokeInput().StrokeStarted(*strokeStarted);
        if (strokeCanceled) presenter.StrokeInput().StrokeCanceled(*strokeCanceled);
        strokesCollected.reset();
        strokesErased.reset();
        strokeStarted.reset();
        strokeCanceled.reset();
    }

    void InkLayer::SetLayout(PageLayout layout)
    {
        ink.layout = std::move(layout); // (after a relocation it is the same)
        SetPenPage(std::nullopt);
        LogHr("IInkPresenterDesktop::SetSize", desktop->SetSize(ink.layout.DocumentSize().width, ink.layout.DocumentSize().height));
        commit();
    }

    void InkLayer::SetPen(PenLook look)
    {
        pen = look;
        ApplyPen();
    }

    void InkLayer::SetDarkPages(std::vector<bool> pages)
    {
        ink.SetDarkPages(std::move(pages));
        ApplyPen();
        ApplyCursor();
    }

    void InkLayer::SetPenPage(std::optional<int> page)
    {
        if (penPage == page) return;
        penPage = page;
        ApplyPen();
        ApplyCursor();
    }

    void InkLayer::SetViewPage(std::optional<int> page)
    {
        if (viewPage != page) penPage.reset();
        viewPage = page;
        ApplyPen();
        ApplyCursor();
    }

    void InkLayer::ApplyPen()
    {
        if (!presenter) return;
        PenLook look = pen;
        if (PenOverDarkPaper())
        {
            look.color = PaperAppearance::DarkToolInk(look.color, look.tool == Tool::Highlighter);
        }
        uint64_t colors = look.color == pen.color ? 0 : (uint64_t{ Pack(pen.color) } << 32) | Pack(look.color);
        if (appliedPen == look)
        {
            penColors->applied = colors;
            return;
        }
        InkDrawingAttributes a;
        a.Color({ look.color.a, look.color.r, look.color.g, look.color.b });
        a.PenTip(PenTipShape::Circle);
        a.FitToCurve(false); // polylines: few-point shapes stay angular
        if (look.tool == Tool::Highlighter)
        {
            a.Size({ look.width, look.width });
            a.IgnorePressure(true);
            a.DrawAsHighlighter(true);
        }
        else
        {
            float size = look.width / InkWidth::FullPressureFactor; // the width is Xournal's: at full pressure
            a.Size({ size, size });
            a.IgnorePressure(false);
        }
        presenter.UpdateDefaultDrawingAttributes(a);
        appliedPen = look;
        penColors->applied = colors;
    }

    void InkLayer::SetMode(InkInputProcessingMode mode)
    {
        auto config = presenter.InputProcessingConfiguration();
        if (config.Mode() != mode) config.Mode(mode); // only the mode: a hovering pen survives that (WinUI fact)
    }

    void InkLayer::SetPenTaken(bool taken)
    {
        if (taken == penTaken) return;
        penTaken = taken;
        Log(std::format("ink: the pen {}", taken ? "goes to the window (selection)" : "comes back to the ink"));
        ApplyDevices();
    }

    void InkLayer::ApplyDevices()
    {
        // Only the pen writes: the finger pans (DirectManipulation) and the mouse selects and taps (window input).
        presenter.InputDeviceTypes(penTaken ? CoreInputDeviceTypes::None : CoreInputDeviceTypes::Pen);
    }

    void InkLayer::SetCursor(UINT resourceId, UINT darkResourceId)
    {
        cursor = resourceId;
        darkCursor = darkResourceId;
        ApplyCursor();
    }

    void InkLayer::ApplyCursor()
    {
        if (!input || !cursorInput->active) return;
        UINT resource = darkCursor && PenOverDarkPaper() ? darkCursor : cursor;
        if (appliedCursor == resource) return;
        input.PointerCursor(resource ? CoreCursor(CoreCursorType::Custom, resource) : nullptr);
        appliedCursor = resource;
        Log(std::format("ink: pen cursor {}", resource));
    }
}
