#pragma once

// Toolbar and menu images drawn at the window's DPI: a Segoe Fluent Icons glyph (Windows 11's icon font) with an
// optional color bar below it (a preset's color), and color swatches for menus. 32-bit premultiplied DIBs.

#include <windows.h>

#include <optional>

#include "inkpages/model/Paper.h"

namespace inkpages::detail
{
    /// A size x size image of the glyph in `ink`, with a bar of `bar` along the bottom if given.
    HBITMAP GlyphIcon(wchar_t glyph, Color ink, std::optional<Color> bar, int size);

    /// The shapes tool's icon (Segoe Fluent Icons has no shapes glyph): a square and a circle overlapping, outlined in
    /// the font's stroke weight, drawn with Direct2D.
    HBITMAP ShapesIcon(Color ink, int size);

    /// A size x size swatch of the color with a thin gray border.
    HBITMAP Swatch(Color color, int size);
}
