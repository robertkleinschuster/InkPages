#include "inkpages/model/TapGesture.h"

#include <algorithm>
#include <cmath>

#include "inkpages/model/Geometry.h"

namespace inkpages
{
    void TapGesture::Press(uint32_t pointer, PointF at, uint64_t ms)
    {
        if (down++ > 0)
        {
            cancelled = true; // a second pointer: a pinch, not a tap
            return;
        }
        first = pointer;
        cancelled = false;
        start = at;
        began = ms;
        farthest = 0;
    }

    void TapGesture::Move(uint32_t pointer, PointF at)
    {
        if (down == 0 || pointer != first) return;
        farthest = std::max(farthest, std::abs(at.x - start.x) + std::abs(at.y - start.y));
    }

    bool TapGesture::Moved() const
    {
        return down > 0 && farthest >= Tap::MaxDistance;
    }

    std::optional<PointF> TapGesture::Release(uint32_t pointer, PointF at, uint64_t ms)
    {
        if (down == 0) return std::nullopt;
        Move(pointer, at);
        bool tap = down == 1 && pointer == first && !cancelled && ms >= began &&
            Tap::IsTap(farthest, static_cast<unsigned>(std::min<uint64_t>(ms - began, UINT32_MAX)));
        if (pointer != first) cancelled = true;
        if (--down > 0) return std::nullopt;
        return tap ? std::optional(start) : std::nullopt;
    }

    void TapGesture::Cancel()
    {
        if (down > 0) cancelled = true;
    }
}
