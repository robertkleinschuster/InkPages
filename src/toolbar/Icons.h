#pragma once

// Toolbar and menu images drawn at the window's DPI: a Segoe Fluent Icons glyph (Windows 11's icon font) with an
// optional color bar below it (a preset's color), and color swatches for menus. 32-bit premultiplied DIBs.

#include <windows.h>

#include <optional>
#include <string_view>

#include "inkpages/model/Paper.h"

namespace inkpages { enum class ToolbarSymbol; }

namespace inkpages::detail
{
    HBITMAP ArtworkIcon(ToolbarSymbol symbol, std::optional<Color> bar, int size);
    /// A size x size image of the glyph in `ink`, with a bar of `bar` along the bottom if given.
    HBITMAP GlyphIcon(wchar_t glyph, Color ink, std::optional<Color> bar, int size);

    /// An app's vector icon: path data (PathData) in a square of `pathSize` units, filled in `ink` (nothing drawn for
    /// path data that does not parse - logged).
    HBITMAP PathIcon(std::wstring_view path, float pathSize, Color ink, int size, std::optional<Color> bar = {});

    /// A size x size swatch of the color with a thin gray border.
    HBITMAP Swatch(Color color, int size);
}
