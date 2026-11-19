#include "toolbar/Icons.h"

#include <d2d1.h>

#include <winrt/base.h>

#include "inkpages/Log.h"
#include "inkpages/model/PathData.h"

#include <algorithm>
#include <functional>

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

    namespace
    {
        // Draws into a size x size DIB with Direct2D (a DC render target bound to the bitmap).
        HBITMAP Draw2D(int size, std::function<void(ID2D1Factory*, ID2D1RenderTarget*)> const& draw)
        {
            uint32_t* bits = nullptr;
            HBITMAP bitmap = Dib(size, bits);
            if (!bitmap) return nullptr;
            std::fill(bits, bits + size * size, 0u);
            HDC dc = CreateCompatibleDC(nullptr);
            HGDIOBJ old = SelectObject(dc, bitmap);
            try
            {
                winrt::com_ptr<ID2D1Factory> factory;
                winrt::check_hresult(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory.put()));
                auto props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
                    D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
                winrt::com_ptr<ID2D1DCRenderTarget> target;
                winrt::check_hresult(factory->CreateDCRenderTarget(&props, target.put()));
                RECT all{ 0, 0, size, size };
                winrt::check_hresult(target->BindDC(dc, &all));
                target->BeginDraw();
                draw(factory.get(), target.get());
                winrt::check_hresult(target->EndDraw());
            }
            catch (...)
            {
                LogException("toolbar icon");
            }
            SelectObject(dc, old);
            DeleteDC(dc);
            return bitmap;
        }

        D2D1_COLOR_F ToD2D(Color c) { return D2D1::ColorF(c.r / 255.f, c.g / 255.f, c.b / 255.f, c.a / 255.f); }
    }

    HBITMAP ShapesIcon(Color ink, int size)
    {
        return Draw2D(size, [&](ID2D1Factory*, ID2D1RenderTarget* target) {
            winrt::com_ptr<ID2D1SolidColorBrush> brush;
            winrt::check_hresult(target->CreateSolidColorBrush(ToD2D(ink), brush.put()));
            float s = static_cast<float>(size), w = s / 16; // Fluent's stroke: 1/16 of the icon
            target->DrawRectangle({ s * 0.12f, s * 0.12f, s * 0.6f, s * 0.6f }, brush.get(), w);
            target->DrawEllipse(D2D1::Ellipse({ s * 0.62f, s * 0.62f }, s * 0.26f, s * 0.26f), brush.get(), w);
        });
    }

    HBITMAP PathIcon(std::wstring const& text, float pathSize, Color ink, int size)
    {
        auto path = ParsePath(text);
        if (!path)
        {
            LogError("toolbar: an app button's path data does not parse");
            return Draw2D(size, [](ID2D1Factory*, ID2D1RenderTarget*) {});
        }
        return Draw2D(size, [&](ID2D1Factory* factory, ID2D1RenderTarget* target) {
            winrt::com_ptr<ID2D1PathGeometry> geometry;
            winrt::check_hresult(factory->CreatePathGeometry(geometry.put()));
            winrt::com_ptr<ID2D1GeometrySink> sink;
            winrt::check_hresult(geometry->Open(sink.put()));
            sink->SetFillMode(path->nonzero ? D2D1_FILL_MODE_WINDING : D2D1_FILL_MODE_ALTERNATE);
            bool open = false;
            using Kind = PathSegment::Kind;
            for (PathSegment const& s : path->segments)
            {
                D2D1_POINT_2F to{ s.to.x, s.to.y };
                switch (s.kind)
                {
                case Kind::Move:
                    if (open) sink->EndFigure(D2D1_FIGURE_END_OPEN);
                    sink->BeginFigure(to, D2D1_FIGURE_BEGIN_FILLED);
                    open = true;
                    break;
                case Kind::Line: sink->AddLine(to); break;
                case Kind::Cubic: sink->AddBezier({ { s.control1.x, s.control1.y }, { s.control2.x, s.control2.y }, to }); break;
                case Kind::Arc:
                    sink->AddArc({ to, { s.radius.width, s.radius.height }, s.rotation,
                        s.clockwise ? D2D1_SWEEP_DIRECTION_CLOCKWISE : D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE,
                        s.largeArc ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL });
                    break;
                case Kind::Close:
                    if (open) sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                    open = false;
                    break;
                }
            }
            if (open) sink->EndFigure(D2D1_FIGURE_END_OPEN);
            winrt::check_hresult(sink->Close());
            winrt::com_ptr<ID2D1SolidColorBrush> brush;
            winrt::check_hresult(target->CreateSolidColorBrush(ToD2D(ink), brush.put()));
            target->SetTransform(D2D1::Matrix3x2F::Scale(size / pathSize, size / pathSize));
            target->FillGeometry(geometry.get(), brush.get());
        });
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
