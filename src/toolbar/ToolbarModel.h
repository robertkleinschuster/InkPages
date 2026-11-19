#pragma once

// What the toolbar shows, without Win32: its buttons in order (groups: history | pens | highlighters | erasers |
// select, shapes, text, ruler | selection commands | the app's buttons), each button's look, whether it is checked or
// enabled, and its tooltip. Unit-tested; Toolbar.cpp maps it onto the comctl32 toolbar.

#include <optional>
#include <string>
#include <vector>

#include "inkpages/Toolbar.h"

namespace inkpages::detail
{
    namespace ButtonId
    {
        constexpr int Undo = 100, Redo = 101;
        constexpr int Pen = 200, Highlighter = 300; // + preset index
        constexpr int Eraser = 400, WholeStrokes = 401;
        constexpr int Select = 500, Shape = 501, Ruler = 502, Text = 503;
        constexpr int Cut = 600, Copy = 601, Paste = 602, Duplicate = 603, Delete = 604, SelectAll = 605;
        constexpr int Extension = 1000; // + index of the app's button
        constexpr int PresetRange = 20;
    }

    enum class ButtonKind { Command, Check, CheckWithMenu };

    struct ButtonSpec
    {
        int id = 0;
        wchar_t glyph = 0;        // 0: a drawn icon (the shapes tool)
        std::optional<Color> bar; // a preset's color
        ButtonKind kind = ButtonKind::Command;
        bool groupStart = false;  // a separator before it
    };

    /// What the toolbar's states depend on besides the choices.
    struct SurfaceStatus
    {
        bool canUndo = false, canRedo = false, hasSelection = false, ruler = false, protractor = false;
    };

    std::vector<ButtonSpec> ToolbarButtons(ToolChoices const& choices, std::vector<ToolbarButton> const& extensions);
    bool IsChecked(int id, ToolChoices const& choices, SurfaceStatus const& status);
    bool IsEnabled(int id, SurfaceStatus const& status);
    /// A width or size in the texts' number format ("0,85 pt" with a decimal comma).
    std::wstring FormatSize(ToolbarTexts const& texts, float value);
    /// The button's accessible name (button text: UI Automation and Narrator read it; never shown on the button).
    std::wstring NameOf(int id, ToolChoices const& choices, ToolbarTexts const& texts, std::vector<ToolbarButton> const& extensions);
    std::wstring TipOf(int id, ToolChoices const& choices, ToolbarTexts const& texts, std::vector<ToolbarButton> const& extensions);
    inline bool IsPreset(int id, int base) { return id >= base && id < base + ButtonId::PresetRange; }
}
