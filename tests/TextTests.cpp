#include <gtest/gtest.h>

#include "inkpages/model/Pens.h"
#include "inkpages/model/Text.h"

namespace inkpages::tests
{
    TEST(RichEditText, TwipsFollowSizeAndZoom)
    {
        EXPECT_EQ(180, RichEditText::Twips(12, 1)) << "12 page units at zoom 1 = 9 pt at the window's DPI";
        EXPECT_EQ(360, RichEditText::Twips(12, 2));
        EXPECT_EQ(90, RichEditText::Twips(12, 0.5f));
    }

    TEST(RichEditText, NewlinesBetweenRichEditAndTheFile)
    {
        EXPECT_EQ(L"a\nb\n\nc", RichEditText::FromRichEdit(L"a\rb\r\rc"));
        EXPECT_EQ(L"a\nb", RichEditText::FromRichEdit(L"a\r\nb")) << "\r\n is one break";
        EXPECT_EQ(L"a\rb\r", RichEditText::ToRichEdit(L"a\nb\n"));
        EXPECT_EQ(L"äöü ß €", RichEditText::FromRichEdit(RichEditText::ToRichEdit(L"äöü ß €")));
    }

    TEST(TextSizes, NearestStep)
    {
        EXPECT_EQ(2, TextSizes::StepOf(12));
        EXPECT_EQ(0, TextSizes::StepOf(1));
        EXPECT_EQ(7, TextSizes::StepOf(100));
    }

    TEST(ToolChoices, TheTextToolsChoicesRoundTrip)
    {
        ToolChoices choices;
        choices.tool = Tool::Text;
        choices.textFont = 3;
        choices.textSize = 20;
        choices.textColor = 2;
        ToolChoices back = ToolChoices::Parse(choices.Serialize());
        EXPECT_EQ(Tool::Text, back.tool);
        EXPECT_EQ(3, back.textFont);
        EXPECT_EQ(20, back.textSize);
        EXPECT_EQ(2, back.textColor);
        TextStyle style = back.Text();
        EXPECT_EQ(L"Consolas", style.font);
        EXPECT_EQ(Palette::Pens[2], style.color);
        ToolChoices damaged = ToolChoices::Parse("text=99:abc:4");
        EXPECT_EQ(0, damaged.textFont);
        EXPECT_EQ(12, damaged.textSize);
        EXPECT_EQ(4, damaged.textColor);
    }
}
