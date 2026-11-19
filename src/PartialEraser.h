#pragma once

// The exact eraser (an agreed exception: Windows ink only erases whole strokes): erases exactly the circle at the pen -
// strokes are cut by EraseCircle and replaced by what is left of them, one undo step per drag. Ported from
// PartialEraser.cs (winui-final). Ink host thread only.

#include <map>
#include <memory>

#include "InkState.h"
#include "StrokeEdit.h"

namespace inkpages::detail
{
    class PartialEraser
    {
    public:
        explicit PartialEraser(InkState& ink) : ink(ink) {}

        float radius = 4; // page units

        void Begin(PointF p);
        /// Erases along the way from the last point through `path` (one pointer event's points, oldest first; circles
        /// every half radius: nothing is skipped) in one pass over the strokes.
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
