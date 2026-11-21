#pragma once

// The exact eraser's geometry (Windows ink only erases whole strokes; a deliberate addition): what is left of a polyline
// stroke when a circle is erased from it. The cut is exact: each segment is intersected with the circle grown by half
// the drawn width there, so the ink's edge - not its centerline - ends at the circle and nothing outside it disappears.
// New end points get the pressure interpolated at the cut. Ported from Model/StrokeEraser.cs (winui-final).

#include <functional>
#include <optional>
#include <vector>

#include "inkpages/model/Paper.h"

namespace inkpages
{
    struct InkSample
    {
        PointF position;
        float pressure = 0.5f;
    };

    using Piece = std::vector<InkSample>;

    /// The pieces left after erasing the circle, or nothing if the circle does not touch the stroke. An empty list:
    /// all of it is erased. `halfWidth`: half the drawn width at a sample.
    std::optional<std::vector<Piece>> EraseCircle(std::vector<InkSample> const& samples,
        std::function<double(InkSample const&)> const& halfWidth, PointF center, double radius);

    std::optional<std::vector<Piece>> EraseSquare(std::vector<InkSample> const& samples,
        std::function<double(InkSample const&)> const& halfWidth, PointF center, double halfSide);
}
