#include <gtest/gtest.h>

#include <algorithm>

#include "toolbar/ToolbarModel.h"

namespace inkpages::tests
{
    using namespace detail;

    TEST(ToolbarModel, GroupsInOrderWithPresetColors)
    {
        ToolChoices choices;
        auto buttons = ToolbarButtons(choices, { { 7, 0xE710, L"app" } });
        std::vector<int> ids;
        for (auto const& b : buttons) ids.push_back(b.id);
        std::vector<int> expected{ ButtonId::Undo, ButtonId::Redo, ButtonId::Pen, ButtonId::Pen + 1, ButtonId::Pen + 2,
            ButtonId::Highlighter, ButtonId::Highlighter + 1, ButtonId::Eraser, ButtonId::WholeStrokes, ButtonId::Select,
            ButtonId::Shape, ButtonId::Ruler, ButtonId::Cut, ButtonId::Copy, ButtonId::Paste, ButtonId::Duplicate,
            ButtonId::Delete, ButtonId::SelectAll, ButtonId::Extension };
        EXPECT_EQ(expected, ids);
        EXPECT_EQ(Palette::Pens[1], buttons[3].bar.value()) << "the blue preset shows its color";
        int groups = static_cast<int>(std::count_if(buttons.begin(), buttons.end(), [](auto const& b) { return b.groupStart; }));
        EXPECT_EQ(6, groups);
    }

    TEST(ToolbarModel, StatesFollowTheChoicesAndTheSurface)
    {
        ToolChoices choices;
        choices.tool = Tool::Highlighter;
        choices.preset = 1;
        choices.wholeStrokeErasing = true;
        SurfaceStatus status{ true, false, false, false, true };
        EXPECT_TRUE(IsChecked(ButtonId::Highlighter + 1, choices, status));
        EXPECT_FALSE(IsChecked(ButtonId::Pen, choices, status));
        EXPECT_TRUE(IsChecked(ButtonId::WholeStrokes, choices, status)) << "a toggle, whatever the tool";
        EXPECT_TRUE(IsChecked(ButtonId::Ruler, choices, status)) << "the protractor is shown";
        EXPECT_TRUE(IsEnabled(ButtonId::Undo, status));
        EXPECT_FALSE(IsEnabled(ButtonId::Redo, status));
        EXPECT_FALSE(IsEnabled(ButtonId::Delete, status)) << "no selection";
        EXPECT_TRUE(IsEnabled(ButtonId::Paste, status));
    }

    TEST(ToolbarModel, SizesUseTheTextsDecimalSeparator)
    {
        ToolbarTexts texts;
        EXPECT_EQ(L"0.85 pt", FormatSize(texts, 0.85f));
        texts.decimalSeparator = L',';
        EXPECT_EQ(L"0,85 pt", FormatSize(texts, 0.85f));
        EXPECT_EQ(L"4 pt", FormatSize(texts, 4));
    }

    TEST(ToolbarModel, TipsNameThePresetAndTheAppsButtons)
    {
        ToolbarTexts texts;
        EXPECT_NE(std::wstring::npos, TipOf(ButtonId::Pen + 2, ToolChoices{}, texts, {}).find(L"Red"));
        EXPECT_EQ(L"app", TipOf(ButtonId::Extension, ToolChoices{}, texts, { { 7, 0xE710, L"app" } }));
        EXPECT_EQ(L"", TipOf(ButtonId::Pen + 9, ToolChoices{}, texts, {}));
    }
}
