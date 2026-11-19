#pragma once

// A sheet's paper: what it is printed with below the ink - nothing, a grid of squares, or lines - and the sheet's color.
// Pure data and geometry (unit-tested); the writing surface draws it with Direct2D, clipped to the sheet.

#include <cstdint>
#include <vector>

namespace inkpages
{
    struct Color
    {
        uint8_t a = 0xFF, r = 0, g = 0, b = 0;
        friend bool operator==(Color, Color) = default;
    };

    struct PointF
    {
        float x = 0, y = 0;
        friend bool operator==(PointF, PointF) = default;
    };

    struct Line
    {
        PointF from, to;
    };

    enum class PaperKind { Plain, Graph, Lined };

    /// Spacing and thickness in page units (DIPs; 24 DIP = 18 pt). Lines start at the sheet's edge (0) and include both
    /// edges where the spacing meets them.
    struct Paper
    {
        /// A light blue-grey that reads as paper ruling on white.
        static constexpr Color DefaultLineColor{ 0xFF, 0xD6, 0xE2, 0xF0 };

        PaperKind kind = PaperKind::Plain;
        float spacing = 24;
        Color lineColor = DefaultLineColor;
        float thickness = 0.8f;
        Color sheetColor{ 0xFF, 0xFF, 0xFF, 0xFF };

        /// The paper's lines on a sheet of this size (page units).
        std::vector<Line> Lines(float width, float height) const;
    };
}
