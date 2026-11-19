// The windowless RichEdit the Text tool edits with, without a screen: text in and out, typing, its size.
#include <gtest/gtest.h>

#include "text/RichEditBox.h"

namespace inkpages::tests
{
    using detail::RichEditBox;

    struct RichEditBoxTest : ::testing::Test
    {
        HWND window = nullptr;
        void SetUp() override
        {
            window = CreateWindowExW(0, L"STATIC", L"", WS_POPUP, 0, 0, 400, 300, nullptr, nullptr, nullptr, nullptr);
            ASSERT_NE(nullptr, window);
        }
        void TearDown() override { DestroyWindow(window); }
    };

    TEST_F(RichEditBoxTest, KeepsPlainTextWithNewlines)
    {
        RichEditBox box(window, 0x1000);
        box.SetText(L"Hallo\nWelt äöü");
        EXPECT_EQ(L"Hallo\nWelt äöü", box.Text());
    }

    TEST_F(RichEditBoxTest, TypedCharactersArrive)
    {
        RichEditBox box(window, 0x1000);
        box.SetClientRect({ 0, 0, 300, 100 });
        box.Focus(true);
        for (wchar_t c : std::wstring(L"Grüße"))  box.Send(WM_CHAR, c, 1);
        box.Send(WM_KEYDOWN, VK_RETURN, 1); // as typing sends it: the key, then its character
        box.Send(WM_CHAR, VK_RETURN, 1);
        box.Send(WM_KEYUP, VK_RETURN, 1);
        box.Send(WM_CHAR, L'ß', 1);
        EXPECT_EQ(L"Grüße\nß", box.Text());
    }

    TEST_F(RichEditBoxTest, ItsNaturalSizeGrowsWithTheText)
    {
        RichEditBox box(window, 0x1000);
        box.SetClientRect({ 0, 0, 300, 100 });
        box.SetFormat(L"Arial", 240, { 0xFF, 0, 0, 0 });
        box.SetText(L"ab");
        SIZE narrow = box.NaturalSize();
        box.SetText(L"abababab");
        SIZE wide = box.NaturalSize();
        box.SetText(L"ab\nab");
        SIZE tall = box.NaturalSize();
        box.SetFormat(L"Arial", 480, { 0xFF, 0, 0, 0 });
        SIZE big = box.NaturalSize();
        EXPECT_GT(narrow.cx, 0);
        EXPECT_GT(narrow.cy, 0);
        EXPECT_GT(wide.cx, narrow.cx * 2);
        EXPECT_GT(tall.cy, narrow.cy * 3 / 2);
        EXPECT_GT(big.cy, tall.cy * 3 / 2);
    }
}
