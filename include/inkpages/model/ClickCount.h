#pragma once

// Counts the mouse's clicks as Windows does for double clicks: a press within the double-click time of the previous one
// and inside the double-click rectangle around it continues the series (2 = double click, 3 = triple click, ...);
// otherwise a new series begins. Windows sends WM_LBUTTONDBLCLK for the second press only - a triple click is counted
// by the app with the same rules (GetDoubleClickTime, SM_CXDOUBLECLK/SM_CYDOUBLECLK). Pure logic (unit-tested).

#include <cstdint>

#include "inkpages/model/Paper.h"

namespace inkpages
{
    class ClickCount
    {
    public:
        /// A press at `at` (window pixels) at `ms`; `maxMs` = the double-click time, `width`/`height` = the double-click
        /// rectangle (centered on the previous press). Returns the press' number in its series (1, 2, 3, ...).
        int Press(PointF at, uint64_t ms, unsigned maxMs, float width, float height);
        /// The series ends (another button, a press elsewhere that no one counts).
        void Reset() { count = 0; }

    private:
        int count = 0;
        PointF last;
        uint64_t lastMs = 0;
    };
}
