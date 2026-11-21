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
            ButtonId::Highlighter, ButtonId::Highlighter + 1, ButtonId::Eraser, ButtonId::WholeStrokes, ButtonId::Shape,
            ButtonId::Select, ButtonId::Text, ButtonId::Ruler, ButtonId::Cut, ButtonId::Copy, ButtonId::Paste, ButtonId::Duplicate,
            ButtonId::Delete, ButtonId::Extension };
        EXPECT_EQ(expected, ids);
        EXPECT_EQ(Palette::Pens[1], buttons[3].bar.value()) << "the blue preset shows its color";
        int groups = static_cast<int>(std::count_if(buttons.begin(), buttons.end(), [](auto const& b) { return b.groupStart; }));
        EXPECT_EQ(6, groups);
    }

    TEST(ToolbarModel, AppButtonsAtTheStartComeBeforeUndo)
    {
        std::vector<ToolbarButton> app{ { 1, 0xE74E, L"save" }, { 2, 0xE72D, L"export" }, { 3, 0xE710, L"page" } };
        app[0].atStart = true;
        app[1].atStart = true;
        auto buttons = ToolbarButtons(ToolChoices{}, app);
        ASSERT_GE(buttons.size(), 4u);
        EXPECT_EQ(ButtonId::Extension, buttons[0].id);
        EXPECT_FALSE(buttons[0].groupStart) << "nothing before the first button";
        EXPECT_EQ(ButtonId::Extension + 1, buttons[1].id);
        EXPECT_FALSE(buttons[1].groupStart);
        EXPECT_EQ(ButtonId::Undo, buttons[2].id);
        EXPECT_TRUE(buttons[2].groupStart) << "a separator between the app's buttons and undo";
        EXPECT_EQ(ButtonId::Extension + 2, buttons.back().id) << "the others stay at the end, ids by their index";
        EXPECT_TRUE(buttons.back().groupStart);
    }

    TEST(ToolbarModel, StatesFollowTheChoicesAndTheSurface)
    {
        ToolChoices choices;
        choices.tool = Tool::Highlighter;
        choices.preset = 1;
        choices.wholeStrokeErasing = true;
        SurfaceStatus status{ true, false, false, false, true };
        choices.ToggleShape(ShapeKind::Arrow);
        EXPECT_TRUE(IsChecked(ButtonId::Highlighter + 1, choices, status));
        EXPECT_TRUE(IsChecked(ButtonId::Shape, choices, status));
        EXPECT_FALSE(IsChecked(ButtonId::Pen, choices, status));
        EXPECT_TRUE(IsChecked(ButtonId::WholeStrokes, choices, status)) << "a toggle, whatever the tool";
        EXPECT_TRUE(IsChecked(ButtonId::Ruler, choices, status)) << "the protractor is shown";
        EXPECT_TRUE(IsEnabled(ButtonId::Undo, status));
        EXPECT_FALSE(IsEnabled(ButtonId::Redo, status));
        EXPECT_FALSE(IsEnabled(ButtonId::Delete, status)) << "no selection";
        EXPECT_TRUE(IsEnabled(ButtonId::Paste, status));
    }

    TEST(ToolbarModel, EveryButtonHasAName)
    {
        ToolbarTexts texts;
        std::vector<ToolbarButton> app{ { 7, 0, L"Insert a page after the page in view", {}, L"Insert page", L"M0,0 L1,1 Z" } };
        for (auto const& b : ToolbarButtons(ToolChoices{}, app)) EXPECT_FALSE(NameOf(b.id, ToolChoices{}, texts, app).empty()) << b.id;
        EXPECT_EQ(L"Pen – Black", NameOf(ButtonId::Pen, ToolChoices{}, texts, app));
        EXPECT_EQ(L"Eraser", NameOf(ButtonId::Eraser, ToolChoices{}, texts, app));
        EXPECT_EQ(L"Insert page", NameOf(ButtonId::Extension, ToolChoices{}, texts, app));
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

    TEST(ToolbarModel, AppButtonsStartGroupsWhereTheyAsk)
    {
        std::vector<ToolbarButton> app(4);
        app[2].groupStart = true; // Speichern, Exportieren | Seite einfuegen, Seite loeschen
        std::vector<bool> starts;
        for (auto const& b : ToolbarButtons(ToolChoices{}, app))
        {
            if (b.id >= ButtonId::Extension) starts.push_back(b.groupStart);
        }
        EXPECT_EQ((std::vector<bool>{ true, false, true, false }), starts);
    }
}
