#pragma once

// The toolbar's popup menus (user32: windowed popups, pen taps reach them), shown below a button: a preset's color and
// width, the exact eraser's size, the selection's mode, the shape and recognition, the ruler or protractor. Each
// returns the changed choices, or nothing when the menu was closed without a choice.

#include <windows.h>

#include <optional>

#include "inkpages/Toolbar.h"

namespace inkpages::detail
{
    std::optional<ToolChoices> PresetMenu(HWND owner, RECT anchor, ToolChoices choices, Tool tool, int index, ToolbarTexts const& texts);
    std::optional<ToolChoices> RadiusMenu(HWND owner, RECT anchor, ToolChoices choices, ToolbarTexts const& texts);
    std::optional<ToolChoices> SelectModeMenu(HWND owner, RECT anchor, ToolChoices choices, ToolbarTexts const& texts);
    std::optional<ToolChoices> ShapeMenu(HWND owner, RECT anchor, ToolChoices choices, ToolbarTexts const& texts);

    struct StencilChoice
    {
        bool ruler = false, protractor = false;
    };
    std::optional<StencilChoice> StencilMenu(HWND owner, RECT anchor, StencilChoice now, ToolbarTexts const& texts);
}
