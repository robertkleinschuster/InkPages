#include "inkpages/model/ClickCount.h"

#include <cmath>

namespace inkpages
{
    int ClickCount::Press(PointF at, uint64_t ms, unsigned maxMs, float width, float height)
    {
        bool continues = count > 0 && ms >= lastMs && ms - lastMs <= maxMs && std::abs(at.x - last.x) <= width / 2 &&
            std::abs(at.y - last.y) <= height / 2;
        count = continues ? count + 1 : 1;
        last = at;
        lastMs = ms;
        return count;
    }
}
