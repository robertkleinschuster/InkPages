#pragma once

// The pen toolbar: a comctl32 toolbar (visual styles, normal mouse size, left-aligned) the app places above or beside
// the writing surface. Groups: undo/redo | pen presets (color and width from a dropdown: Xournal's palette and widths;
// a second tap on the active preset opens it too) | highlighter presets | eraser + "erase whole strokes" toggle (its
// dropdown: the exact eraser's size) | the app's own buttons. (Selection, shapes and the ruler join in later steps.)
// Texts come from the app (English defaults). Tool choices are the app's to keep: `choicesChanged`.

#include <windows.h>

#include <array>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "inkpages/Pens.h"
#include "inkpages/WritingSurface.h"

namespace inkpages
{
    struct ToolbarTexts
    {
        std::wstring pen = L"Pen";
        std::wstring highlighter = L"Highlighter";
        std::wstring presetTip = L"tap again for color and size";
        std::wstring eraser = L"Eraser";
        std::wstring eraserTip = L"Eraser – the pen's eraser button erases too, with any tool";
        std::wstring wholeStrokeErase = L"Erase whole strokes";
        std::wstring wholeStrokeEraseTip = L"Erase whole strokes: the eraser and the pen's eraser button erase every "
            L"stroke they touch. Off: only what is in the circle. The arrow: the circle's size";
        std::wstring eraserRadius = L"Size";
        std::wstring undo = L"Undo";
        std::wstring redo = L"Redo";
        std::wstring color = L"Color";
        std::wstring width = L"Width";
        /// A width or size in the menus; {} is the number.
        std::wstring widthFormat = L"{} pt";
        std::array<std::wstring, 10> penColors{ L"Black", L"Blue", L"Red", L"Green", L"Gray", L"Light blue",
            L"Light green", L"Magenta", L"Orange", L"Yellow" };
        std::array<std::wstring, 5> highlighterColors{ L"Yellow", L"Green", L"Light blue", L"Magenta", L"Orange" };
    };

    /// An app's own button at the end of the toolbar.
    struct ToolbarButton
    {
        int id = 0;            // the app's id (any number)
        wchar_t glyph = 0;     // a Segoe Fluent Icons glyph
        std::wstring tip;
        std::function<void()> click;
    };

    class Toolbar
    {
    public:
        /// `cursorFor`: the pen cursor (cursor resource in the app's exe) for a preset; 0 or none: Windows' default.
        /// The toolbar chains the surface's `historyChanged` (set yours before creating the toolbar).
        static std::unique_ptr<Toolbar> Create(HWND parent, WritingSurface& surface, ToolChoices choices,
            ToolbarTexts texts = {}, std::function<UINT(Tool, PenPreset const&)> cursorFor = {},
            std::vector<ToolbarButton> extensions = {});
        virtual ~Toolbar() = default;

        virtual HWND Window() const = 0;
        /// The height the toolbar wants (pixels), for the app's layout.
        virtual int Height() const = 0;
        virtual ToolChoices const& Choices() const = 0;
        /// Sets the choices as if the user made them (e.g. the app's keyboard shortcuts for tools).
        virtual void Choose(ToolChoices choices) = 0;
        virtual void EnableButton(int id, bool enabled) = 0;

        /// The user changed a choice (UI thread): save it.
        std::function<void(ToolChoices const&)> choicesChanged;
    };
}
