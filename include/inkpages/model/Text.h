#pragma once

// Texts on the pages, as Xournal++ keeps them (<text font size color x y>): plain text with its newlines, one font, size
// and color per text, placed by its top left. And the arithmetic between page units and the RichEdit that edits one
// (pure, unit-tested).

#include <array>
#include <string>

#include "inkpages/model/Paper.h"

namespace inkpages
{
    struct TextStyle
    {
        std::wstring font = L"Arial"; // a Windows font family
        float size = 12;              // the font size in page units (Xournal++: points)
        Color color{ 0xFF, 0, 0, 0 };
        friend bool operator==(TextStyle const&, TextStyle const&) = default;
    };

    /// The fonts and sizes offered in the Text tool's menu. Arial first: what Xournal++'s default "Sans" comes out as on
    /// Windows.
    struct TextFonts
    {
        static constexpr std::array<wchar_t const*, 5> Faces{ L"Arial", L"Segoe UI", L"Times New Roman", L"Consolas", L"Cambria Math" };
    };

    struct TextSizes
    {
        static constexpr std::array<float, 8> Steps{ 8, 10, 12, 14, 16, 20, 24, 32 };
        static int StepOf(float size);
    };

    /// Between the library's texts and the windowless RichEdit that edits one.
    struct RichEditText
    {
        /// RichEdit's font height (twips) for a text of `size` page units shown at `zoom`: RichEdit lays out at the
        /// window's DPI (a point = DPI/72 pixels) while a page unit is shown as scale * zoom pixels (scale = DPI/96), so
        /// the text is size * zoom * 72/96 points to RichEdit.
        static long Twips(float size, float zoom);
        /// RichEdit ends paragraphs with \r; texts keep \n (as the file does).
        static std::wstring FromRichEdit(std::wstring const& text);
        static std::wstring ToRichEdit(std::wstring const& text);
    };
}
