#include "ink/InkLayer.h"

#include <format>

#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.Core.h>

#include "ink/StrokeEdit.h"
#include "inkpages/Log.h"

namespace inkpages::detail
{
    using namespace winrt::Windows::UI::Input::Inking;
    using winrt::Windows::UI::Core::CoreCursor;
    using winrt::Windows::UI::Core::CoreCursorType;
    using winrt::Windows::UI::Core::CoreInputDeviceTypes;
    using winrt::Windows::UI::Core::PointerEventArgs;

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
        // The presenter's objects are released on their own thread - waited for without a timeout: the work refers to
        // this layer, which must not go before it ran.
        try
        {
            host.Run("release presenter", [this] {
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

    void InkLayer::Subscribe()
    {
        presenter.StrokesCollected([this](InkPresenter const&, InkStrokesCollectedEventArgs const& e) {
            try
            {
                auto edit = std::make_shared<StrokeEdit>(ink);
                std::vector<Placed> placed;
                for (InkStroke const& stroke : e.Strokes())
                {
                    if (drop && drop(stroke))
                    {
                        ink.Remove({ stroke });
                        continue;
                    }
                    auto pieces = ink.Split(stroke);
                    if (pieces.size() != 1 || pieces[0].stroke != stroke)
                    {
                        ink.Remove({ stroke }); // it crossed a page's edge: its pieces take its place
                        for (Placed const& p : pieces) ink.container.AddStroke(p.stroke);
                    }
                    for (Placed const& p : pieces)
                    {
                        ink.owner[p.stroke.Id()] = p.page;
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
        input.PointerEntering([this](auto&&, PointerEventArgs const&) {
            try
            {
                if (inputActive.exchange(true)) return;
                Log("ink: the presenter's input is active (first pointer event)");
                host.Post("cursor", [this] { ApplyCursor(); });
            }
            catch (...)
            {
                LogException("ink: pointer entering");
            }
        });
    }

    void InkLayer::SetLayout(PageLayout layout)
    {
        ink.layout = std::move(layout); // (after a relocation it is the same)
        LogHr("IInkPresenterDesktop::SetSize", desktop->SetSize(ink.layout.DocumentSize().width, ink.layout.DocumentSize().height));
        commit();
    }

    void InkLayer::SetPen(PenLook look)
    {
        pen = look;
        if (!presenter) return;
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

    void InkLayer::SetCursor(UINT resourceId)
    {
        cursor = resourceId;
        ApplyCursor();
    }

    void InkLayer::ApplyCursor()
    {
        if (!input || !inputActive) return;
        input.PointerCursor(cursor ? CoreCursor(CoreCursorType::Custom, cursor) : nullptr);
        Log(std::format("ink: pen cursor {}", cursor));
    }
}
