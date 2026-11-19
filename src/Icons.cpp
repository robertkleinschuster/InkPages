#include "Icons.h"

#include <algorithm>

namespace inkpages::detail
{
    namespace
    {
        HBITMAP Dib(int size, uint32_t*& bits)
        {
            BITMAPINFO bi{};
            bi.bmiHeader = { sizeof(BITMAPINFOHEADER), size, -size, 1, 32, BI_RGB };
            void* raw = nullptr;
            HBITMAP bitmap = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &raw, nullptr, 0);
            bits = static_cast<uint32_t*>(raw);
            return bitmap;
        }

        uint32_t Premultiplied(Color c, unsigned alpha)
        {
            alpha = alpha * c.a / 255;
            return (alpha << 24) | ((c.r * alpha / 255) << 16) | ((c.g * alpha / 255) << 8) | (c.b * alpha / 255);
        }

        void Box(uint32_t* bits, int size, int top, int bottom, Color fill)
        {
            Color border{ 0xFF, 0x80, 0x80, 0x80 };
            int left = size / 8, right = size - size / 8;
            for (int y = top; y < bottom; ++y)
            {
                for (int x = left; x < right; ++x)
                {
                    bool edge = y == top || y == bottom - 1 || x == left || x == right - 1;
                    bits[y * size + x] = Premultiplied(edge ? border : fill, 255);
                }
            }
        }
    }

    HBITMAP GlyphIcon(wchar_t glyph, Color ink, std::optional<Color> bar, int size)
    {
        uint32_t* bits = nullptr;
        HBITMAP bitmap = Dib(size, bits);
        if (!bitmap) return nullptr;
        int glyphSize = bar ? size * 3 / 4 : size;
        HDC dc = CreateCompatibleDC(nullptr);
        HGDIOBJ old = SelectObject(dc, bitmap);
        std::fill(bits, bits + size * size, 0u);
        HFONT font = CreateFontW(-glyphSize * 7 / 8, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
            ANTIALIASED_QUALITY, 0, L"Segoe Fluent Icons");
        HGDIOBJ oldFont = SelectObject(dc, font);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(255, 255, 255));
        RECT r{ 0, 0, size, glyphSize };
        DrawTextW(dc, &glyph, 1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        GdiFlush();
        // White on black: the red channel is the glyph's coverage.
        for (int i = 0; i < size * size; ++i) bits[i] = Premultiplied(ink, (bits[i] >> 16) & 0xFF);
        if (bar) Box(bits, size, size - size / 4, size - 1, *bar);
        SelectObject(dc, oldFont);
        DeleteObject(font);
        SelectObject(dc, old);
        DeleteDC(dc);
        return bitmap;
    }

    HBITMAP Swatch(Color color, int size)
    {
        uint32_t* bits = nullptr;
        HBITMAP bitmap = Dib(size, bits);
        if (!bitmap) return nullptr;
        std::fill(bits, bits + size * size, 0u);
        Box(bits, size, size / 8, size - size / 8, color);
        return bitmap;
    }
}
