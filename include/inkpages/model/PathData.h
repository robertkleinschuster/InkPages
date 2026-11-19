#pragma once

// Path data as XAML and SVG write it ("F1 M5,9.3 V3.5 L5.6,2.1 ... A4.5,4.5 0 1 1 5,19.5 Z"): parsed into absolute
// segments for drawing (the toolbar's icons of app buttons). Commands M L H V C A Z, absolute and relative; F0 even-odd
// (the default) or F1 nonzero fill. Pure (unit-tested).

#include <optional>
#include <string_view>
#include <vector>

#include "inkpages/model/PageLayout.h"

namespace inkpages
{
    struct PathSegment
    {
        enum class Kind { Move, Line, Cubic, Arc, Close };
        Kind kind = Kind::Move;
        PointF to;
        PointF control1, control2;  // Cubic
        SizeF radius;               // Arc
        float rotation = 0;         // Arc, degrees
        bool largeArc = false, clockwise = false;
    };

    struct PathData
    {
        bool nonzero = false;       // F1
        std::vector<PathSegment> segments;
    };

    /// Nothing for text that is not path data.
    std::optional<PathData> ParsePath(std::wstring_view text);
}
