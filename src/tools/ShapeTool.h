#pragma once

// The shape tool (ink thread, on unprocessed input): a line, arrow, rectangle or ellipse dragged out, shown while
// dragging, then added as one stroke of even width - as Xournal++ stores shapes. Ported from ShapeTool.cs.

#include <memory>

#include "ink/InkState.h"
#include "ink/StrokeEdit.h"
#include "render/Overlay.h"

namespace inkpages::detail
{
    struct ShapeLook
    {
        ShapeKind kind = ShapeKind::Line;
        Color color{ 0xFF, 0, 0, 0 };
        float width = 1.41f; // the pen's width at full pressure: shapes have no pressure
        bool highlighter = false;
    };

    class ShapeTool
    {
    public:
        ShapeTool(InkState& ink, OverlayModel& overlay) : ink(ink), overlay(overlay) {}

        void Begin(PointF p, ShapeLook look);
        void Move(PointF p);
        /// The shape's edit (empty for a tap: no shape).
        std::shared_ptr<StrokeEdit> End(PointF p);
        void Cancel();
        bool Running() const { return running; }

    private:
        InkState& ink;
        OverlayModel& overlay;
        ShapeLook look;
        PointF start;
        bool running = false;
    };

    /// A stroke along points in a pen's look, without pressure (shapes and recognized shapes).
    InkStroke ShapeStroke(std::vector<PointF> const& outline, Color color, float width, bool highlighter = false);
}
