#pragma once

// Shape recognition by Windows ink analysis (InkAnalyzer): a stroke Windows recognizes as a circle, ellipse, triangle,
// rectangle or other polygon gets a clean outline. Asynchronous: the result comes back on the ink thread. Ported from
// ShapeRecognition.cs (winui-final).

#include <functional>
#include <optional>
#include <vector>

#include "ink/InkHost.h"
#include "ink/Strokes.h"

namespace inkpages::detail
{
    class ShapeRecognizer
    {
    public:
        explicit ShapeRecognizer(InkHost& host) : host(host) {}

        /// `done` gets the clean outline, or nothing when Windows sees no shape (ink thread).
        void Recognize(InkStroke const& stroke, std::function<void(std::optional<std::vector<PointF>>)> done);

    private:
        InkHost& host;
    };
}
