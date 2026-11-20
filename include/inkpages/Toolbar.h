#pragma once

// The pen toolbar: a comctl32 toolbar (visual styles, normal mouse size, left-aligned) the app places above or beside
// the writing surface. Groups: undo/redo | pen presets (color and width from a dropdown: Xournal's palette and widths;
// a second tap on the active preset opens it too) | highlighter presets | eraser + "erase whole strokes" toggle (its
// dropdown: the exact eraser's size) | select (lasso or rectangle), shapes (kind, recognition), Windows' ruler and
// protractor | the selection's commands | the app's own buttons.
// Texts come from the app (English defaults). Tool choices are the app's to keep: `choicesChanged`.

#include <windows.h>

#include <array>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "inkpages/model/Pens.h"
#include "inkpages/WritingSurface.h"

namespace inkpages
{
    struct ToolbarTexts
    {
        std::wstring pen = L"Pen";
        std::wstring highlighter = L"Highlighter";
        std::wstring presetTip = L"tap again for color and size";
        std::wstring writeToolTip = L"Only the pen writes. The mouse and touch navigate.";
        std::wstring eraser = L"Eraser";
        std::wstring eraserTip = L"Eraser – the pen's eraser button erases too, with any tool";
        std::wstring wholeStrokeErase = L"Erase whole strokes";
        std::wstring wholeStrokeEraseTip = L"Erase whole strokes: the eraser and the pen's eraser button erase every "
            L"stroke they touch. Off: only what is in the circle. The arrow: the circle's size";
        std::wstring eraserRadius = L"Size";
        std::wstring select = L"Select";
        std::wstring selectTip = L"Select: lasso or rectangle (the arrow); drag to move, corners to scale. With the "
            L"pen, a lasso works with any tool: drag with the barrel button held";
        std::wstring lasso = L"Lasso";
        std::wstring rectangle = L"Rectangle";
        std::wstring shapes = L"Shapes";
        std::wstring shapesTip = L"Shapes: line, arrow, rectangle, ellipse (the arrow); drawn shapes can be recognized";
        std::array<std::wstring, 4> shapeKinds{ L"Line", L"Arrow", L"Rectangle", L"Ellipse" };
        std::wstring recognizeShapes = L"Recognize drawn shapes";
        std::wstring text = L"Text";
        std::wstring textTip = L"Text: tap on the page to write (the keyboard types), tap a text to edit it, tap beside "
            L"it to finish. The arrow: font, size and color";
        std::wstring ruler = L"Ruler";
        std::wstring protractor = L"Protractor";
        std::wstring rulerTip = L"Ruler / protractor (the arrow): lines drawn along it come out straight";
        std::wstring cut = L"Cut selection";
        std::wstring copy = L"Copy selection";
        std::wstring paste = L"Paste";
        std::wstring duplicate = L"Duplicate selection";
        std::wstring deleteSelection = L"Delete selection";
        std::wstring selectAll = L"Select all on the page";
        std::wstring undo = L"Undo";
        std::wstring redo = L"Redo";
        std::wstring color = L"Color";
        std::wstring width = L"Width";
        /// A width or size in the menus; {} is the number, written with `decimalSeparator`.
        std::wstring widthFormat = L"{} pt";
        wchar_t decimalSeparator = L'.';
        std::array<std::wstring, 10> penColors{ L"Black", L"Blue", L"Red", L"Green", L"Gray", L"Light blue",
            L"Light green", L"Magenta", L"Orange", L"Yellow" };
        std::array<std::wstring, 5> highlighterColors{ L"Yellow", L"Green", L"Light blue", L"Magenta", L"Orange" };
    };

    /// An app's own button at the end of the toolbar (or, with `atStart`, before undo/redo): a Segoe Fluent Icons glyph, or a vector path where the font has no
    /// fitting glyph (XAML/SVG path data with M L H V C A Z, absolute or relative, F0 even-odd / F1 nonzero fill, in a
    /// square of `pathSize` units).
    struct ToolbarButton
    {
        int id = 0;            // the app's id (any number)
        wchar_t glyph = 0;
        std::wstring tip;      // the tooltip
        std::function<void()> click;
        std::wstring name;     // its accessible name (UI Automation, Narrator); empty: the tip
        std::wstring path;     // used when glyph is 0
        float pathSize = 20;
        bool groupStart = false; // a separator before it (the first app button at the end always has one)
        bool atStart = false;    // placed at the toolbar's start, before undo/redo, as apps put Save and Export there;
                                 // a separator follows the last of them
    };

    class Toolbar
    {
    public:
        /// `cursorFor`: the pen cursor (cursor resource in the app's exe) for a preset; 0 or none: Windows' default.
        /// The toolbar chains the surface's `historyChanged` and `selectionChanged` (set yours before creating it).
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
