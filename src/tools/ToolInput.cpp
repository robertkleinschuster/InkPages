#include "tools/ToolInput.h"

#include <algorithm>
#include <cmath>
#include <format>

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

    ToolInput::ToolInput(InkLayer& ink, OverlayModel& overlay)
        : ink(ink), overlay(overlay), eraser(ink.State()), selection(ink.State()), shapes(ink.State(), overlay),
          recognizer(ink.Host()), stencils(ink.Presenter())
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

    void ToolInput::Subscribe()
    {
        auto events = ink.Presenter().UnprocessedInput();
        auto guard = [](char const* what, auto&& f) {
            try { f(); } catch (...) { LogException(std::format("tools: {}", what)); }
        };
        events.PointerPressed([this, guard](InkUnprocessedInput const&, PointerEventArgs const& e) {
            guard("pressed", [&] {
                auto p = e.CurrentPoint();
                PointF at = At(p);
                bool barrel = IsBarrel(p);
                ink.Host().Post("tool press", [this, at, barrel] { Press(at, barrel); });
            });
        });
        events.PointerMoved([this, guard](InkUnprocessedInput const&, PointerEventArgs const& e) {
            guard("moved", [&] {
                auto path = Path(e);
                ink.Host().Post("tool move", [this, path] { Move(path); });
            });
        });
        events.PointerReleased([this, guard](InkUnprocessedInput const&, PointerEventArgs const& e) {
            guard("released", [&] {
                PointF at = At(e.CurrentPoint());
                ink.Host().Post("tool release", [this, at] { Release(at); });
            });
        });
        events.PointerLost([this, guard](InkUnprocessedInput const&, PointerEventArgs const&) {
            guard("lost", [&] {
                ink.Host().Post("tool lost", [this] {
                    shapes.Cancel(); // a lost pointer leaves no shape; a lost erase keeps what it erased
                    lasso.clear();
                    overlay.SetLasso({});
                    if (eraser.Running()) ink.Edits().Push(eraser.End());
                    gesture = Gesture::None;
                });
            });
        });
        // The presenter's hover (Windows' input thread): read the point here, act on the ink thread.
        auto hover = [this, guard](PointerEventArgs const& e, bool leaving, char const* what) {
            guard(what, [&] {
                auto p = e.CurrentPoint();
                if (p.IsInContact()) return;
                Hover h{ At(p), IsPen(p), IsEraserEnd(p), leaving, IsBarrel(p) };
                ink.Host().Post("tool hover", [this, h] { OnHover(h); });
            });
        };
        auto input = ink.Input();
        input.PointerEntering([hover](auto&&, PointerEventArgs const& e) { hover(e, false, "entering"); });
        input.PointerHovering([hover](auto&&, PointerEventArgs const& e) { hover(e, false, "hovering"); });
        input.PointerExiting([hover](auto&&, PointerEventArgs const& e) { hover(e, true, "exiting"); });
        input.PointerMoving([this, guard](auto&&, PointerEventArgs const& e) {
            guard("moving", [&] {
                auto p = e.CurrentPoint();
                if (!p.IsInContact()) return;
                PointF at = At(p);
                ink.Host().Post("circle", [this, at] {
                    if (gesture == Gesture::Erase) overlay.SetCircle(at, settings.eraserRadius);
                });
            });
        });
    }

    void ToolInput::SetTool(ToolSettings next)
    {
        settings = next;
        eraser.radius = next.eraserRadius;
        if (next.tool == Tool::Eraser || next.wholeStrokeErasing) eraserEndTaken = false;
        if (next.tool != Tool::Eraser || next.wholeStrokeErasing) overlay.SetCircle(std::nullopt, 0);
        ApplyMode();
    }

    void ToolInput::ApplyMode()
    {
        InkInputProcessingMode mode = InkInputProcessingMode::Inking;
        switch (settings.tool)
        {
        case Tool::Eraser: mode = settings.wholeStrokeErasing ? InkInputProcessingMode::Erasing : InkInputProcessingMode::None; break;
        case Tool::Select:
        case Tool::Shape: mode = InkInputProcessingMode::None; break;
        default: mode = eraserEndTaken ? InkInputProcessingMode::None : InkInputProcessingMode::Inking; break;
        }
        ink.SetMode(mode);
    }

    void ToolInput::SetFrame(std::optional<SelectionFrame> f)
    {
        frame = f;
        overlay.SetFrame(f);
        if (!f) GiveBackPen();
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
        bool exact = !settings.wholeStrokeErasing;
        bool circle = !h.leaving && ((settings.tool == Tool::Eraser && exact) || (h.inverted && exact));
        overlay.SetCircle(circle ? std::optional(h.at) : std::nullopt, settings.eraserRadius);
        if (!h.pen || h.leaving) return;
        // The pen's eraser end erases exactly: taken before it touches (only the mode changes), given back for the tip.
        bool take = h.inverted && exact && settings.tool != Tool::Eraser;
        if (take != eraserEndTaken)
        {
            eraserEndTaken = take;
            Log(std::format("tools: the pen's eraser end {}", take ? "taken (exact eraser)" : "given back"));
            ApplyMode();
        }
        // Over the selection the tip (or the barrel) moves and scales it: the pen goes to the window.
        if (frame && !h.inverted && frame->At(h.at) != Handle::None && !penTakenNow)
        {
            penTakenNow = true;
            ink.SetPenTaken(true);
            if (penTaken) penTaken(true);
        }
    }

    void ToolInput::Press(PointF at, bool barrel)
    {
        if (barrel || settings.tool == Tool::Select)
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
        else if ((settings.tool == Tool::Eraser && !settings.wholeStrokeErasing) || eraserEndTaken)
        {
            gesture = Gesture::Erase;
            eraser.Begin(at);
            overlay.SetCircle(at, settings.eraserRadius);
        }
        else if (settings.tool == Tool::Shape)
        {
            gesture = Gesture::Shape;
            shapes.Begin(at, settings.shape);
        }
    }

    void ToolInput::Move(std::vector<PointF> const& path)
    {
        switch (gesture)
        {
        case Gesture::Erase: eraser.MoveAlong(path); break;
        case Gesture::Shape: shapes.Move(path.back()); break;
        case Gesture::Select:
            if (rectangle) overlay.SetLasso(RectanglePolygon(lasso.front(), path.back()));
            else
            {
                lasso.insert(lasso.end(), path.begin(), path.end());
                overlay.SetLasso(lasso);
            }
            break;
        default: break;
        }
    }

    void ToolInput::Release(PointF at)
    {
        switch (gesture)
        {
        case Gesture::Erase:
            if (eraser.Running()) ink.Edits().Push(eraser.End());
            break;
        case Gesture::Shape: ink.Edits().Push(shapes.End(at)); break;
        case Gesture::Select:
        {
            std::vector<PointF> polygon = rectangle ? RectanglePolygon(lasso.front(), at) : lasso;
            overlay.SetLasso({});
            lasso.clear();
            StrokeSelection made = selection.Select(polygon);
            Log(std::format("tools: {} selected {} strokes", rectangle ? "rectangle" : "lasso", made.ids.size()));
            if (selected) selected(made, polygon);
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
        if (!Tap::IsTap(farthest, ms)) return false;
        Log("tools: a tap beside the selection clears it");
        selection.Clear();
        if (cleared) cleared();
        return true;
    }

    void ToolInput::Recognize(std::vector<Placed> const& placed)
    {
        if (!settings.recognizeShapes || settings.tool != Tool::Pen || placed.size() != 1) return;
        InkStroke drawn = placed.front().stroke;
        auto attributes = drawn.DrawingAttributes();
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
