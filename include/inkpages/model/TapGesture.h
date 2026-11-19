#pragma once

// A tap of the mouse or a finger, told from a drag: pressed and released within Tap's rules (under 300 ms, moved under
// 3 DIPs on the screen at the zoom of the press), by one pointer alone (a second finger makes it a pinch) and not taken by anyone else meanwhile (a
// pan, a lost capture). Pure logic (unit-tested); the window feeds it document points and its clock.

#include <cstdint>
#include <optional>

#include "inkpages/model/Paper.h"

namespace inkpages
{
    class TapGesture
    {
    public:
        /// `zoom`: the view's at the press (points are document points; the tap distance is the screen's).
        void Press(uint32_t pointer, PointF at, uint64_t ms, float zoom = 1);
        void Move(uint32_t pointer, PointF at);
        /// The first pointer left the tap distance: a drag.
        bool Moved() const;
        /// The press point when this release ends a tap; none otherwise. The gesture ends with the last pointer.
        std::optional<PointF> Release(uint32_t pointer, PointF at, uint64_t ms);
        /// Someone else took the gesture (DirectManipulation pans, the capture was lost): no tap.
        void Cancel();
        bool Active() const { return down > 0; }

    private:
        uint32_t first = 0;
        int down = 0;
        bool cancelled = false;
        PointF start;
        uint64_t began = 0;
        float farthest = 0;
        float zoom = 1;
    };
}
