#pragma once

// Geometry-based pen eraser with a configurable footprint and one undo step per drag.

#include <map>
#include <memory>

#include "ink/InkState.h"
#include "ink/StrokeEdit.h"

namespace inkpages::detail
{
    class ExactEraser
    {
    public:
        explicit ExactEraser(InkState& ink) : ink(ink) {}

        float radius = 4; // page units
        bool square = false;
        bool wholeStrokes = false;

        void Begin(PointF p);
        /// Erases along the way from the last point through `path` (one pointer event's points, oldest first; circles
        /// or squares every half extent) in one pass over the strokes.
        void MoveAlong(std::vector<PointF> const& path);
        /// The drag ended (or was canceled - what was erased stays erased): its edit, for the history.
        std::shared_ptr<StrokeEdit> End();
        bool Running() const { return edit != nullptr; }

    private:
        struct Sampled
        {
            std::vector<InkSample> samples;
            InkDrawingAttributes attributes{ nullptr };
            float size = 1;
            bool ignorePressure = false;
        };
        void EraseAt(std::vector<PointF> const& centers);

        InkState& ink;
        std::shared_ptr<StrokeEdit> edit;
        PointF last;
        std::map<uint32_t, Sampled> sampled; // strokes read so far this drag (each read from the ink once)
    };
}
