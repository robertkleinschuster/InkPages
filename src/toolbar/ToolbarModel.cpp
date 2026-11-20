#include "toolbar/ToolbarModel.h"

#include <algorithm>
#include <format>

namespace inkpages::detail
{
    namespace Glyph // Segoe Fluent Icons
    {
        constexpr wchar_t Undo = 0xE7A7, Redo = 0xE7A6, Pen = 0xE76D, Highlighter = 0xE7E6, Eraser = 0xE75C,
            WholeStrokes = 0xED62, Lasso = 0xF1BE, Shapes = 0, Text = 0xE8D2, Ruler = 0xED5E, Cut = 0xE8C6, Copy = 0xE8C8,
            Paste = 0xE77F, Duplicate = 0xF413, Delete = 0xE74D, SelectAll = 0xE8B3;
    }

    std::vector<ButtonSpec> ToolbarButtons(ToolChoices const& choices, std::vector<ToolbarButton> const& extensions)
    {
        using enum ButtonKind;
        std::vector<ButtonSpec> b;
        auto appButton = [&](size_t i, bool groupStart) { // glyph 0: its path
            b.push_back({ ButtonId::Extension + static_cast<int>(i), extensions[i].glyph, std::nullopt, Command, groupStart });
        };
        for (size_t i = 0; i < extensions.size(); ++i)
        {
            if (extensions[i].atStart) appButton(i, !b.empty() && extensions[i].groupStart);
        }
        b.push_back({ ButtonId::Undo, Glyph::Undo, std::nullopt, Command, !b.empty() });
        b.push_back({ ButtonId::Redo, Glyph::Redo });
        auto pens = choices.PensOrDefault();
        for (int i = 0; i < static_cast<int>(pens.size()); ++i)
        {
            b.push_back({ ButtonId::Pen + i, Glyph::Pen, Palette::Pens[pens[i].color], CheckWithMenu, i == 0 });
        }
        auto highlighters = choices.HighlightersOrDefault();
        for (int i = 0; i < static_cast<int>(highlighters.size()); ++i)
        {
            b.push_back({ ButtonId::Highlighter + i, Glyph::Highlighter, Palette::Highlighters[highlighters[i].color], CheckWithMenu, i == 0 });
        }
        b.push_back({ ButtonId::Eraser, Glyph::Eraser, std::nullopt, Check, true });
        b.push_back({ ButtonId::WholeStrokes, Glyph::WholeStrokes, std::nullopt, CheckWithMenu });
        b.push_back({ ButtonId::Shape, Glyph::Shapes, std::nullopt, CheckWithMenu });
        b.push_back({ ButtonId::Select, Glyph::Lasso, std::nullopt, CheckWithMenu, true });
        b.push_back({ ButtonId::Text, Glyph::Text, Palette::Pens[std::clamp(choices.textColor, 0, 9)], CheckWithMenu });
        b.push_back({ ButtonId::Ruler, Glyph::Ruler, std::nullopt, CheckWithMenu });
        b.push_back({ ButtonId::Cut, Glyph::Cut, std::nullopt, Command, true });
        b.push_back({ ButtonId::Copy, Glyph::Copy });
        b.push_back({ ButtonId::Paste, Glyph::Paste });
        b.push_back({ ButtonId::Duplicate, Glyph::Duplicate });
        b.push_back({ ButtonId::Delete, Glyph::Delete });
        b.push_back({ ButtonId::SelectAll, Glyph::SelectAll });
        bool first = true;
        for (size_t i = 0; i < extensions.size(); ++i)
        {
            if (extensions[i].atStart) continue;
            appButton(i, first || extensions[i].groupStart);
            first = false;
        }
        return b;
    }

    std::wstring FormatSize(ToolbarTexts const& texts, float value)
    {
        std::wstring number = std::format(L"{}", value);
        std::replace(number.begin(), number.end(), L'.', texts.decimalSeparator);
        return std::vformat(texts.widthFormat, std::make_wformat_args(number));
    }

    bool IsChecked(int id, ToolChoices const& c, SurfaceStatus const& s)
    {
        if (IsPreset(id, ButtonId::Pen)) return c.tool == Tool::Pen && c.preset == id - ButtonId::Pen;
        if (IsPreset(id, ButtonId::Highlighter)) return c.tool == Tool::Highlighter && c.preset == id - ButtonId::Highlighter;
        switch (id)
        {
        case ButtonId::Eraser: return c.tool == Tool::Eraser;
        case ButtonId::WholeStrokes: return c.wholeStrokeErasing;
        case ButtonId::Select: return c.tool == Tool::Select;
        case ButtonId::Shape: return c.tool == Tool::Shape;
        case ButtonId::Text: return c.tool == Tool::Text;
        case ButtonId::Ruler: return s.ruler || s.protractor;
        default: return false;
        }
    }

    bool IsEnabled(int id, SurfaceStatus const& s)
    {
        switch (id)
        {
        case ButtonId::Undo: return s.canUndo;
        case ButtonId::Redo: return s.canRedo;
        case ButtonId::Cut:
        case ButtonId::Copy:
        case ButtonId::Duplicate:
        case ButtonId::Delete: return s.hasSelection;
        default: return true;
        }
    }

    std::wstring NameOf(int id, ToolChoices const& c, ToolbarTexts const& t, std::vector<ToolbarButton> const& extensions)
    {
        auto preset = [](std::wstring const& kind, std::wstring const& color) { return std::format(L"{} – {}", kind, color); };
        if (IsPreset(id, ButtonId::Pen))
        {
            auto pens = c.PensOrDefault();
            return id - ButtonId::Pen < static_cast<int>(pens.size()) ? preset(t.pen, t.penColors[pens[id - ButtonId::Pen].color]) : L"";
        }
        if (IsPreset(id, ButtonId::Highlighter))
        {
            auto hl = c.HighlightersOrDefault();
            return id - ButtonId::Highlighter < static_cast<int>(hl.size()) ? preset(t.highlighter, t.highlighterColors[hl[id - ButtonId::Highlighter].color]) : L"";
        }
        if (id >= ButtonId::Extension && id - ButtonId::Extension < static_cast<int>(extensions.size()))
        {
            auto const& b = extensions[id - ButtonId::Extension];
            return b.name.empty() ? b.tip : b.name;
        }
        switch (id)
        {
        case ButtonId::Eraser: return t.eraser;
        case ButtonId::WholeStrokes: return t.wholeStrokeErase;
        case ButtonId::Select: return t.select;
        case ButtonId::Shape: return t.shapes;
        case ButtonId::Text: return t.text;
        case ButtonId::Ruler: return t.ruler;
        default: return TipOf(id, c, t, extensions); // the commands' tips are their names
        }
    }

    std::wstring TipOf(int id, ToolChoices const& c, ToolbarTexts const& t, std::vector<ToolbarButton> const& extensions)
    {
        auto writeTip = [&](std::wstring const& tip) { return tip + L"\n" + t.writeToolTip; };
        auto preset = [&](std::wstring const& kind, std::wstring const& color) {
            return writeTip(std::format(L"{} – {} ({})", kind, color, t.presetTip));
        };
        if (IsPreset(id, ButtonId::Pen))
        {
            auto pens = c.PensOrDefault();
            return id - ButtonId::Pen < static_cast<int>(pens.size()) ? preset(t.pen, t.penColors[pens[id - ButtonId::Pen].color]) : L"";
        }
        if (IsPreset(id, ButtonId::Highlighter))
        {
            auto hl = c.HighlightersOrDefault();
            return id - ButtonId::Highlighter < static_cast<int>(hl.size()) ? preset(t.highlighter, t.highlighterColors[hl[id - ButtonId::Highlighter].color]) : L"";
        }
        if (id >= ButtonId::Extension && id - ButtonId::Extension < static_cast<int>(extensions.size())) return extensions[id - ButtonId::Extension].tip;
        switch (id)
        {
        case ButtonId::Undo: return t.undo;
        case ButtonId::Redo: return t.redo;
        case ButtonId::Eraser: return writeTip(t.eraserTip);
        case ButtonId::WholeStrokes: return t.wholeStrokeEraseTip;
        case ButtonId::Select: return t.selectTip;
        case ButtonId::Shape: return writeTip(t.shapesTip);
        case ButtonId::Text: return t.textTip;
        case ButtonId::Ruler: return t.rulerTip;
        case ButtonId::Cut: return t.cut;
        case ButtonId::Copy: return t.copy;
        case ButtonId::Paste: return t.paste;
        case ButtonId::Duplicate: return t.duplicate;
        case ButtonId::Delete: return t.deleteSelection;
        case ButtonId::SelectAll: return t.selectAll;
        default: return {};
        }
    }
}
