#pragma once

// Pens and what the user chose in the toolbar: pure data and arithmetic (unit-tested). Ported from the WinUI version
// (Model/PenWidths.cs, Model/ToolChoices.cs, Model/InkStrokes.cs, XournalPalette.cs at tag winui-final).

#include <array>
#include <string>
#include <vector>

#include "inkpages/Paper.h"

namespace inkpages
{
    /// The pens' widths: Xournal++'s pen widths (very fine, fine, medium, thick) and two thicker ones - the width at full
    /// pressure, in page units (the document's unit, as the pages' sizes).
    struct PenWidths
    {
        static constexpr std::array<float, 6> Steps{ 0.42f, 0.85f, 1.41f, 2.26f, 3.4f, 5.67f };

        /// The step (0-based) whose width is nearest to a width.
        static int StepOf(float width);
    };

    /// The highlighter's widths offered in its menu (drawn width, no pressure); Xournal's default is 8.5.
    struct HighlighterWidths
    {
        static constexpr std::array<float, 4> Steps{ 6, 8.5f, 12, 16 };
        static int StepOf(float width);
    };

    /// How Windows ink draws a stroke (measured): a stroke of size s at pressure p is about s * (0.25 + 1.5 p) wide,
    /// exactly s at p = 0.5. Xournal's pen width is the width at full pressure: a pen of width w draws with size
    /// w / FullPressureFactor, so it is never wider than w.
    struct InkWidth
    {
        static constexpr float FullPressureFactor = 1.75f;
        static float Factor(float pressure) { return 0.25f + 1.5f * pressure; }
        static float PressureFor(float factor) { return (factor - 0.25f) / 1.5f; }
        static float Drawn(float size, bool ignorePressure, float pressure) { return ignorePressure ? size : size * Factor(pressure); }
    };

    /// Xournal++'s default palette for pens and its usual highlighter colors (yellow first).
    struct Palette
    {
        static constexpr std::array<Color, 10> Pens{ {
            { 0xFF, 0x00, 0x00, 0x00 }, // black
            { 0xFF, 0x33, 0x33, 0xCC }, // blue
            { 0xFF, 0xFF, 0x00, 0x00 }, // red
            { 0xFF, 0x00, 0x80, 0x00 }, // green
            { 0xFF, 0x80, 0x80, 0x80 }, // gray
            { 0xFF, 0x00, 0xC0, 0xFF }, // light blue
            { 0xFF, 0x00, 0xFF, 0x00 }, // light green
            { 0xFF, 0xFF, 0x00, 0xFF }, // magenta
            { 0xFF, 0xFF, 0x80, 0x00 }, // orange
            { 0xFF, 0xFF, 0xFF, 0x00 }, // yellow
        } };
        static constexpr std::array<Color, 5> Highlighters{ {
            { 0xFF, 0xFF, 0xFF, 0x00 }, // yellow
            { 0xFF, 0x00, 0xFF, 0x00 }, // green
            { 0xFF, 0x00, 0xC0, 0xFF }, // light blue
            { 0xFF, 0xFF, 0x00, 0xFF }, // magenta
            { 0xFF, 0xFF, 0x80, 0x00 }, // orange
        } };
    };

    /// A pen or highlighter preset: one tap selects it; it keeps its own color (index into its palette) and width
    /// (pens: Xournal's width at full pressure; highlighters: the drawn width).
    struct PenPreset
    {
        int color = 0;
        float width = 1.41f;
        friend bool operator==(PenPreset const&, PenPreset const&) = default;
    };

    enum class Tool { Pen, Highlighter, Eraser };

    /// What the user chose in the toolbar and keeps across sessions. The app stores it (e.g. with `Serialize`) where it
    /// keeps its settings, creates the toolbar from it and saves it when the toolbar says it changed.
    struct ToolChoices
    {
        static constexpr float DefaultPenWidth = 1.41f;        // Xournal's medium pen
        static constexpr float DefaultHighlighterWidth = 8.5f; // Xournal's highlighter
        static constexpr float DefaultEraserRadius = 4;

        std::vector<PenPreset> pens;          // empty: DefaultPens
        std::vector<PenPreset> highlighters;  // empty: DefaultHighlighters
        float eraserRadius = DefaultEraserRadius; // the exact eraser's circle radius (page points, 1..20)
        bool wholeStrokeErasing = false;      // the eraser erases whole strokes (Windows ink's eraser) instead of the circle
        Tool tool = Tool::Pen;                // the active tool
        int preset = 0;                       // the active pen or highlighter preset

        static std::vector<PenPreset> DefaultPens() { return { { 0, DefaultPenWidth }, { 1, DefaultPenWidth }, { 2, DefaultPenWidth } }; }
        static std::vector<PenPreset> DefaultHighlighters() { return { { 0, DefaultHighlighterWidth }, { 1, DefaultHighlighterWidth } }; }
        std::vector<PenPreset> PensOrDefault() const { return pens.empty() ? DefaultPens() : pens; }
        std::vector<PenPreset> HighlightersOrDefault() const { return highlighters.empty() ? DefaultHighlighters() : highlighters; }

        /// One line of text ("pens=0:1.41,1:1.41;highlighters=...;eraser=4;whole=0;tool=0;preset=0"); `Parse` reads it
        /// back - what it does not understand keeps its default.
        std::string Serialize() const;
        static ToolChoices Parse(std::string const& text);
    };
}
