#pragma once

// The toolbar's popup menus (user32: windowed popups, pen taps reach them), shown below a button: a preset's color and
// width, the exact eraser's size, the selection's mode, the shape and recognition, the text's font, size and color, the ruler or protractor. Each
// returns the changed choices, or nothing when the menu was closed without a choice.

#include <windows.h>

#include <optional>

#include "inkpages/Toolbar.h"

namespace inkpages::detail
{
    bool MeasureToolMenu(HWND owner, MEASUREITEMSTRUCT& item);
    bool DrawToolMenu(HWND owner, DRAWITEMSTRUCT const& item, ToolbarColors const& colors);

    std::optional<ToolChoices> PresetMenu(HWND owner, RECT anchor, ToolChoices choices, Tool tool, int index, ToolbarTexts const& texts,
        std::optional<ToolbarColors> const& colors, bool darkPaper);
    std::optional<ToolChoices> RadiusMenu(HWND owner, RECT anchor, ToolChoices choices, ToolbarTexts const& texts, std::optional<ToolbarColors> const& colors);
    std::optional<ToolChoices> SelectModeMenu(HWND owner, RECT anchor, ToolChoices choices, ToolbarTexts const& texts, std::optional<ToolbarColors> const& colors);
    std::optional<ToolChoices> LaserMenu(HWND owner, RECT anchor, ToolChoices choices,
        std::optional<ToolbarColors> const& colors);
    std::optional<ToolChoices> TextMenu(HWND owner, RECT anchor, ToolChoices choices, ToolbarTexts const& texts,
        std::optional<ToolbarColors> const& colors, bool darkPaper);
    std::optional<ToolChoices> ShapeMenu(HWND owner, RECT anchor, ToolChoices choices, ToolbarTexts const& texts, std::optional<ToolbarColors> const& colors);

    struct StencilChoice
    {
        bool ruler = false, protractor = false;
    };
    std::optional<StencilChoice> StencilMenu(HWND owner, RECT anchor, StencilChoice now, ToolbarTexts const& texts, std::optional<ToolbarColors> const& colors);
}
