#include "tools/ToolInput.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <mutex>

#include <winrt/Windows.Devices.Input.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.Core.h>
#include <winrt/Windows.UI.Input.h>

#include "inkpages/Log.h"

namespace inkpages::detail
{
    using namespace winrt::Windows::UI::Input::Inking;
    using winrt::Windows::Devices::Input::PointerDeviceType;
    using winrt::Windows::UI::Core::PointerEventArgs;
    using winrt::Windows::UI::Input::PointerPoint;

    namespace
    {
        bool IsPen(PointerPoint const& p) { return p.PointerDevice().PointerDeviceType() == PointerDeviceType::Pen; }
        // The pen turned over: inverted while hovering, inverted and/or eraser while touching.
        bool IsEraserEnd(PointerPoint const& p) { return IsPen(p) && (p.Properties().IsInverted() || p.Properties().IsEraser()); }
        bool IsBarrel(PointerPoint const& p) { return p.Properties().IsBarrelButtonPressed() || p.Properties().IsRightButtonPressed(); }
        PointF At(PointerPoint const& p) { return { p.Position().X, p.Position().Y }; }

        // A pointer event's points, oldest first (the docs give no order for GetIntermediatePoints: sorted by time).
        std::vector<PointF> Path(PointerEventArgs const& e)
        {
            std::vector<std::pair<uint64_t, PointF>> timed;
            for (auto const& p : e.GetIntermediatePoints()) timed.push_back({ p.Timestamp(), At(p) });
            std::sort(timed.begin(), timed.end(), [](auto const& a, auto const& b) { return a.first < b.first; });
            std::vector<PointF> path;
            for (auto const& t : timed) path.push_back(t.second);
            if (path.empty()) path.push_back(At(e.CurrentPoint()));
            return path;
        }

        std::vector<PointF> RectanglePolygon(PointF a, PointF b) { return { a, { b.x, a.y }, b, { a.x, b.y } }; }
    }

    struct ToolInput::PointerDispatch : std::enable_shared_from_this<PointerDispatch>
    {
        explicit PointerDispatch(ToolInput& tools) : target(&tools), host(tools.ink.Host()) {}

        template<class Work>
        void Post(char const* name, Work work)
        {
            auto invoke = [state = shared_from_this(), work = std::move(work)] {
                if (state->target) work(*state->target);
            };
            if (GetCurrentThreadId() == inkThread)
            {
                // Inline work may call the presenter, which can raise pointer events on its input thread.
                invoke();
                return;
            }
            std::scoped_lock lock(mutex);
            if (target) host.Post(name, std::move(invoke));
        }

        void Close()
        {
            std::scoped_lock lock(mutex);
            target = nullptr;
        }

        // Only the ink thread changes target. Other threads may test it under the lock before queueing.
        ToolInput* target;
        InkHost& host;
        const DWORD inkThread = GetCurrentThreadId();
        std::mutex mutex;
    };

    ToolInput::ToolInput(InkLayer& ink, OverlayModel& overlay)
        : ink(ink), overlay(overlay), eraser(ink.State()), selection(ink.State()), shapes(ink.State(), overlay),
          recognizer(ink.Host()), stencils(ink.Presenter())
    {
        pointerDispatch = std::make_shared<PointerDispatch>(*this);
        try
        {
            ink.drop = [this](InkStroke const& stroke) { return DropTap(stroke); };
            ink.collected = [this](std::vector<Placed> const& placed) {
                if (frame && cleared) // writing beside the selection clears it
                {
                    selection.Clear();
                    cleared();
                }
                Recognize(placed);
            };
            Subscribe();
            ApplyMode();
        }
        catch (...)
        {
            Unsubscribe();
            throw;
        }
    }

    ToolInput::~ToolInput() { Unsubscribe(); }

    void ToolInput::Unsubscribe() noexcept
    {
        pointerDispatch->Close();
        ink.drop = {};
        ink.collected = {};
        if (pressed) events.PointerPressed(*pressed);
        if (moved) events.PointerMoved(*moved);
        if (released) events.PointerReleased(*released);
        if (lost) events.PointerLost(*lost);
        auto const& input = ink.Input();
        if (entering) input.PointerEntering(*entering);
        if (hovering) input.PointerHovering(*hovering);
        if (exiting) input.PointerExiting(*exiting);
        if (moving) input.PointerMoving(*moving);
    }

    void ToolInput::Subscribe()
    {
        events = ink.Presenter().UnprocessedInput();
        auto dispatch = pointerDispatch;
        auto guard = [](char const* what, auto&& f) {
            try { f(); } catch (...) { LogException(std::format("tools: {}", what)); }
        };
        pressed = events.PointerPressed([dispatch, guard](InkUnprocessedInput const&, PointerEventArgs const& e) {
            guard("pressed", [&] {
                auto p = e.CurrentPoint();
                PointF at = At(p);
                bool barrel = IsBarrel(p);
                bool inverted = IsEraserEnd(p);
                dispatch->Post("tool press", [at, barrel, inverted](ToolInput& tools) { tools.Press(at, barrel, inverted); });
            });
        });
        moved = events.PointerMoved([dispatch, guard](InkUnprocessedInput const&, PointerEventArgs const& e) {
            guard("moved", [&] {
                auto path = Path(e);
                dispatch->Post("tool move", [path = std::move(path)](ToolInput& tools) { tools.Move(path); });
            });
        });
        released = events.PointerReleased([dispatch, guard](InkUnprocessedInput const&, PointerEventArgs const& e) {
            guard("released", [&] {
                PointF at = At(e.CurrentPoint());
                dispatch->Post("tool release", [at](ToolInput& tools) { tools.Release(at); });
            });
        });
        lost = events.PointerLost([dispatch, guard](InkUnprocessedInput const&, PointerEventArgs const&) {
            guard("lost", [&] {
                dispatch->Post("tool lost", [](ToolInput& tools) { tools.Cancel(); });
            });
        });
        // The presenter's hover (Windows' input thread): read the point here, act on the ink thread.
        auto hover = [dispatch, guard](PointerEventArgs const& e, bool leaving, char const* what) {
            guard(what, [&] {
                auto p = e.CurrentPoint();
                if (p.IsInContact()) return;
                Hover h{ At(p), IsPen(p), IsEraserEnd(p), leaving, IsBarrel(p) };
                dispatch->Post("tool hover", [h](ToolInput& tools) { tools.OnHover(h); });
            });
        };
        auto input = ink.Input();
        entering = input.PointerEntering([hover](auto&&, PointerEventArgs const& e) { hover(e, false, "entering"); });
        hovering = input.PointerHovering([hover](auto&&, PointerEventArgs const& e) { hover(e, false, "hovering"); });
        exiting = input.PointerExiting([hover](auto&&, PointerEventArgs const& e) { hover(e, true, "exiting"); });
        moving = input.PointerMoving([dispatch, guard](auto&&, PointerEventArgs const& e) {
            guard("moving", [&] {
                auto p = e.CurrentPoint();
                if (!p.IsInContact()) return;
                PointF at = At(p);
                dispatch->Post("circle", [at](ToolInput& tools) {
                    if (tools.gesture == Gesture::Erase)
                        tools.overlay.SetEraser(at, tools.settings.eraserRadius, tools.settings.squareEraser);
                });
            });
        });
    }

    void ToolInput::Cancel()
    {
        if (gesture != Gesture::None) Log(std::format("tools: gesture {} ends without its release", static_cast<int>(gesture)));
        shapes.Cancel(); // a lost pointer leaves no shape; a lost erase keeps what it erased
        lasso.clear();
        overlay.SetLasso({});
        mouseGesture = false;
        if (eraser.Running()) ink.Edits().Push(eraser.End());
        gesture = Gesture::None;
    }

    void ToolInput::SetTool(ToolSettings next)
    {
        settings = next;
        eraser.radius = next.eraserRadius;
        eraser.square = next.squareEraser;
        eraser.wholeStrokes = next.wholeStrokeErasing;
        overlay.SetEraser(std::nullopt, 0, next.squareEraser);
        ApplyMode();
    }

    void ToolInput::ApplyMode()
    {
        InkInputProcessingMode mode = InkInputProcessingMode::Inking;
        switch (settings.tool)
        {
        case Tool::Eraser:
        case Tool::Select:
        case Tool::CopyArea:
        case Tool::Laser:
        case Tool::Text: mode = InkInputProcessingMode::None; break;
        default: mode = settings.shapeEnabled ? InkInputProcessingMode::None : InkInputProcessingMode::Inking; break;
        }
        if (eraserEndHover) mode = InkInputProcessingMode::None;
        ink.SetMode(mode);
    }

    void ToolInput::SetFrame(std::optional<SelectionFrame> f)
    {
        frame = f;
        overlay.SetFrame(f);
        if (!f && !textBox) GiveBackPen();
    }

    void ToolInput::PreviewSelection(std::optional<Affine> transform)
    {
        if (transform) selection.Preview(*transform);
        else selection.EndPreview();
        if (!frame) return;
        Affine m = transform.value_or(Affine{});
        SelectionFrame shown = *frame;
        if (m.m12 != 0 || m.m21 != 0 || m.m11 < 0 || m.m22 < 0 || (m.m11 == 1 && m.m22 == 1))
            shown.orientation = m;
        else shown.bounds = m.Bounds(frame->bounds); // scaling keeps the handles' size on screen
        overlay.SetFrame(shown);
    }

    void ToolInput::SetTextBox(std::optional<RectF> box)
    {
        textBox = box;
        if (!box && !frame) GiveBackPen();
    }

    void ToolInput::GiveBackPen()
    {
        if (!penTakenNow) return;
        penTakenNow = false;
        ink.SetPenTaken(false);
        if (penTaken) penTaken(false);
    }

    void ToolInput::OnHover(Hover const& h)
    {
        bool showEraser = !h.leaving && (settings.tool == Tool::Eraser || h.inverted);
        overlay.SetEraser(showEraser ? std::optional(h.at) : std::nullopt, settings.eraserRadius, settings.squareEraser);
        if (!h.pen) return;
        ink.SetPenPage(h.leaving ? std::nullopt : ink.State().layout.PageAt(h.at));
        bool inverted = h.inverted && !h.leaving;
        if (inverted != eraserEndHover)
        {
            eraserEndHover = inverted;
            ApplyMode();
        }
        if (h.leaving) return;
        // Over the selection the tip (or the barrel) moves and scales it, over the edited text box it edits there: the
        // pen goes to the window.
        bool overSelection = (settings.tool != Tool::CopyArea || h.barrel) && frame && frame->At(h.at) != Handle::None;
        bool overText = textBox && textBox->Contains(h.at);
        if ((overSelection || overText) && !h.inverted && !penTakenNow)
        {
            penTakenNow = true;
            ink.SetPenTaken(true);
            if (penTaken) penTaken(true);
        }
    }

    void ToolInput::Press(PointF at, bool barrel, bool inverted)
    {
        if (mouseGesture) // the pen takes over from the mouse
        {
            mouseGesture = false;
            gesture = Gesture::None;
            lasso.clear();
            overlay.SetLasso({});
        }
        if (!inverted && !barrel && settings.tool == Tool::CopyArea)
        {
            gesture = Gesture::CopyArea;
            lasso = { at };
        }
        else if (!inverted && (barrel || settings.tool == Tool::Select))
        {
            gesture = Gesture::Select;
            rectangle = !barrel && settings.selectMode == SelectMode::Rectangle; // the barrel always lassos
            lasso = { at };
            if (frame && cleared)
            {
                selection.Clear();
                cleared();
            }
        }
        else if (settings.tool == Tool::Eraser || inverted)
        {
            gesture = Gesture::Erase;
            eraser.Begin(at);
            overlay.SetEraser(at, settings.eraserRadius, settings.squareEraser);
        }
        else if (settings.tool == Tool::Laser)
        {
            gesture = Gesture::Laser;
            laserAt = at;
            Move({ at });
        }
        else if (settings.shapeEnabled)
        {
            gesture = Gesture::Shape;
            shapes.Begin(at, settings.shape);
        }
        else if (settings.tool == Tool::Text)
        {
            gesture = Gesture::Text; // a press without a drag places or edits a text
            textPress = at;
            textPressMs = GetTickCount64();
            textFarthest = 0;
            Log(std::format("tools: text press at {:.1f},{:.1f}", at.x, at.y));
        }
    }

    void ToolInput::MouseSelectBegin(std::vector<PointF> const& path, bool forceLasso)
    {
        if (path.empty() || gesture != Gesture::None) return; // the pen's gesture runs
        mouseGesture = true;
        gesture = !forceLasso && settings.tool == Tool::CopyArea ? Gesture::CopyArea : Gesture::Select;
        rectangle = !forceLasso && settings.selectMode == SelectMode::Rectangle;
        lasso = { path.front() };
        Move(path);
    }

    void ToolInput::MouseSelectMove(std::vector<PointF> const& path)
    {
        if (mouseGesture) Move(path);
    }

    void ToolInput::MouseSelectEnd(PointF at)
    {
        if (!mouseGesture) return;
        mouseGesture = false;
        Release(at);
    }

    void ToolInput::MouseSelectCancel()
    {
        if (!mouseGesture) return;
        mouseGesture = false;
        gesture = Gesture::None;
        lasso.clear();
        overlay.SetLasso({});
    }

    void ToolInput::Move(std::vector<PointF> const& path)
    {
        switch (gesture)
        {
        case Gesture::Erase: eraser.MoveAlong(path); break;
        case Gesture::Laser:
            laserAt = overlay.AddLaser(laserAt, path, 4 / Zoom(), settings.laserSeconds);
            break;
        case Gesture::Shape: shapes.Move(path.back()); break;
        case Gesture::Text: // how far the pen went: a drag is no text
            for (PointF p : path) textFarthest = std::max(textFarthest, std::abs(p.x - textPress.x) + std::abs(p.y - textPress.y));
            break;
        case Gesture::Select:
            if (rectangle) overlay.SetLasso(RectanglePolygon(lasso.front(), path.back()));
            else
            {
                lasso.insert(lasso.end(), path.begin(), path.end());
                overlay.SetLasso(lasso);
            }
            break;
        case Gesture::CopyArea:
            overlay.SetLasso(RectanglePolygon(lasso.front(), path.back()));
            break;
        default: break;
        }
    }

    void ToolInput::Release(PointF at)
    {
        switch (gesture)
        {
        case Gesture::Laser: Move({ at }); break;
        case Gesture::Erase:
            if (eraser.Running())
            {
                auto edit = eraser.End();
                Log(std::format("tools: exact erase ({} pages)", edit->Pages().size()));
                ink.Edits().Push(edit);
            }
            break;
        case Gesture::Shape:
        {
            auto edit = shapes.End(at);
            Log(std::format("tools: shape ({} strokes)", edit->Empty() ? 0 : 1));
            ink.Edits().Push(edit);
            break;
        }
        case Gesture::Text:
        {
            // Where to write is the press point; only a drag is not a text. How long the pen rests does not matter: a
            // real pen's tap is often held longer than Tap's 300 ms.
            textFarthest = std::max(textFarthest, std::abs(at.x - textPress.x) + std::abs(at.y - textPress.y));
            unsigned ms = static_cast<unsigned>(GetTickCount64() - textPressMs);
            bool tap = textFarthest < Tap::MaxDistance(Zoom());
            Log(std::format("tools: text release, moved {:.2f} units in {} ms (tap distance {:.2f}): {}", textFarthest, ms,
                Tap::MaxDistance(Zoom()), tap ? "a text there" : "a drag, no text"));
            if (tap && textTapped) textTapped(textPress);
            break;
        }
        case Gesture::Select:
        {
            std::vector<PointF> polygon = rectangle ? RectanglePolygon(lasso.front(), at) : lasso;
            overlay.SetLasso({});
            lasso.clear();
            StrokeSelection made = selection.Select(polygon);
            Log(std::format("tools: {} selected {} strokes", rectangle ? "rectangle" : "lasso", made.ids.size()));
            if (selected) selected(made, polygon, rectangle);
            break;
        }
        case Gesture::CopyArea:
        {
            auto start = lasso.front();
            overlay.SetLasso({});
            lasso.clear();
            if (areaPicked) areaPicked(start, at);
            break;
        }
        default: break;
        }
        gesture = Gesture::None;
    }

    // A tip tap beside the selection clears it and leaves no dot.
    bool ToolInput::DropTap(InkStroke const& stroke)
    {
        if (!frame) return false;
        auto samples = SamplesOf(stroke);
        float farthest = 0;
        for (InkSample const& s : samples)
        {
            farthest = std::max(farthest, std::abs(s.position.x - samples.front().position.x) + std::abs(s.position.y - samples.front().position.y));
        }
        auto duration = stroke.StrokeDuration();
        unsigned ms = duration ? static_cast<unsigned>(duration.Value().count() / 10000) : 0;
        if (!Tap::IsTap(farthest, ms, Zoom())) return false;
        Log("tools: a tap beside the selection clears it");
        selection.Clear();
        if (cleared) cleared();
        return true;
    }

    void ToolInput::Recognize(std::vector<Placed> const& placed)
    {
        if (!settings.recognizeShapes || settings.tool != Tool::Pen || placed.size() != 1) return;
        InkStroke drawn = placed.front().stroke;
        auto attributes = ink.State().OriginalAttributes(drawn);
        auto c = attributes.Color();
        Color color{ c.A, c.R, c.G, c.B };
        float width = attributes.Size().Width * InkWidth::FullPressureFactor; // shapes draw the pen's full width
        recognizer.Recognize(drawn, [this, drawn, color, width](std::optional<std::vector<PointF>> outline) {
            auto& state = ink.State();
            if (!outline || state.Find({ drawn.Id() }).empty()) return; // no shape, or the stroke is gone meanwhile
            auto edit = std::make_shared<StrokeEdit>(state);
            edit->Removed(drawn, state.PageOf(drawn));
            state.Remove({ drawn });
            for (Placed const& p : state.Split(ShapeStroke(*outline, color, width)))
            {
                state.Add(p.stroke, p.page);
                edit->Added(p.stroke, p.page);
            }
            ink.Edits().Push(edit);
        });
    }
}
