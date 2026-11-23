#include "toolbar/Icons.h"

#include <d2d1.h>
#include <wincodec.h>

#include <winrt/base.h>

#include "inkpages/Log.h"
#include "inkpages/Toolbar.h"
#include "inkpages/model/PathData.h"

#include <algorithm>
#include <functional>

namespace inkpages
{
    HBITMAP CreateToolbarSymbolBitmap(ToolbarSymbol symbol, int size)
    {
        return detail::ArtworkIcon(symbol, std::nullopt, size);
    }

    HBITMAP CreateFluentGlyphBitmap(wchar_t glyph, COLORREF color, int size)
    {
        if (size <= 0) throw winrt::hresult_invalid_argument(L"Glyph bitmap size");
        return detail::GlyphIcon(glyph, { 0xFF, GetRValue(color), GetGValue(color), GetBValue(color) }, {}, size);
    }
}

namespace inkpages::detail
{
    namespace
    {
        constexpr int AtlasCell = 64, AtlasColumns = 5, AtlasRows = 6;
        BYTE atlasPng[] = {
#include "toolbar-atlas.inc"
        };

        struct Artwork
        {
            winrt::com_ptr<IWICImagingFactory> factory;
            winrt::com_ptr<IWICBitmap> atlas;

            Artwork()
            {
                factory = winrt::create_instance<IWICImagingFactory>(CLSID_WICImagingFactory);
                winrt::com_ptr<IWICStream> stream;
                winrt::check_hresult(factory->CreateStream(stream.put()));
                winrt::check_hresult(stream->InitializeFromMemory(atlasPng, static_cast<DWORD>(sizeof(atlasPng))));
                winrt::com_ptr<IWICBitmapDecoder> decoder;
                winrt::check_hresult(factory->CreateDecoderFromStream(stream.get(), nullptr, WICDecodeMetadataCacheOnLoad, decoder.put()));
                winrt::com_ptr<IWICBitmapFrameDecode> frame;
                winrt::check_hresult(decoder->GetFrame(0, frame.put()));
                UINT width = 0, height = 0;
                winrt::check_hresult(frame->GetSize(&width, &height));
                if (width != AtlasColumns * AtlasCell || height != AtlasRows * AtlasCell)
                    throw winrt::hresult_error(E_FAIL, L"Invalid toolbar atlas size");
                winrt::com_ptr<IWICFormatConverter> pixels;
                winrt::check_hresult(factory->CreateFormatConverter(pixels.put()));
                winrt::check_hresult(pixels->Initialize(frame.get(), GUID_WICPixelFormat32bppPBGRA,
                    WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom));
                winrt::check_hresult(factory->CreateBitmapFromSource(pixels.get(), WICBitmapCacheOnLoad, atlas.put()));
            }
        };

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

    HBITMAP ArtworkIcon(ToolbarSymbol symbol, std::optional<Color> bar, int size)
    {
        int index = static_cast<int>(symbol);
        if (index < 0 || index >= AtlasColumns * AtlasRows || size <= 0)
            throw winrt::hresult_error(E_INVALIDARG, L"Invalid toolbar artwork request");
        thread_local Artwork artwork;
        WICRect cell{ index % AtlasColumns * AtlasCell, index / AtlasColumns * AtlasCell, AtlasCell, AtlasCell };
        winrt::com_ptr<IWICBitmapClipper> clipper;
        winrt::check_hresult(artwork.factory->CreateBitmapClipper(clipper.put()));
        winrt::check_hresult(clipper->Initialize(artwork.atlas.get(), &cell));
        UINT pixels = static_cast<UINT>(bar ? size * 3 / 4 : size);
        winrt::com_ptr<IWICBitmapScaler> scaler;
        winrt::check_hresult(artwork.factory->CreateBitmapScaler(scaler.put()));
        winrt::check_hresult(scaler->Initialize(clipper.get(), pixels, pixels, WICBitmapInterpolationModeFant));

        uint32_t* bits = nullptr;
        HBITMAP bitmap = Dib(size, bits);
        if (!bitmap) return nullptr;
        std::fill(bits, bits + size * size, 0u);
        UINT offset = (static_cast<UINT>(size) - pixels) / 2;
        constexpr UINT BytesPerPixel = sizeof(uint32_t);
        UINT stride = static_cast<UINT>(size) * BytesPerPixel;
        HRESULT copied = scaler->CopyPixels(nullptr, stride, stride * static_cast<UINT>(size) - offset * BytesPerPixel,
            reinterpret_cast<BYTE*>(bits + offset));
        if (FAILED(copied))
        {
            if (!DeleteObject(bitmap)) LogError("DeleteObject (failed toolbar artwork) failed");
            winrt::check_hresult(copied);
        }
        if (bar) Box(bits, size, size - size / 4, size - 1, *bar);
        return bitmap;
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
        struct IconTarget
        {
            winrt::com_ptr<ID2D1Factory> factory;
            winrt::com_ptr<ID2D1DCRenderTarget> target;
            HDC dc = nullptr;

            ~IconTarget()
            {
                target = nullptr;
                if (dc && !DeleteDC(dc)) LogError("DeleteDC (toolbar icon) failed");
            }

            void Ensure()
            {
                if (!factory) winrt::check_hresult(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory.put()));
                if (!dc)
                {
                    dc = CreateCompatibleDC(nullptr);
                    winrt::check_pointer(dc);
                }
                if (!target)
                {
                    auto props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
                        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96);
                    winrt::check_hresult(factory->CreateDCRenderTarget(&props, target.put()));
                }
            }
        };

        // Draws into a size x size DIB with Direct2D (a DC render target bound to the bitmap).
        HBITMAP Draw2D(int size, std::function<void(ID2D1Factory*, ID2D1RenderTarget*)> const& draw)
        {
            thread_local IconTarget renderer;
            uint32_t* bits = nullptr;
            HBITMAP bitmap = Dib(size, bits);
            if (!bitmap) return nullptr;
            std::fill(bits, bits + size * size, 0u);
            HGDIOBJ old = nullptr;
            bool drawn = false;
            try
            {
                renderer.Ensure();
                old = SelectObject(renderer.dc, bitmap);
                winrt::check_pointer(old);
                RECT all{ 0, 0, size, size };
                auto const& target = renderer.target;
                winrt::check_hresult(target->BindDC(renderer.dc, &all));
                target->BeginDraw();
                target->SetTransform(D2D1::Matrix3x2F::Identity());
                target->Clear(D2D1::ColorF(0, 0.0f));
                draw(renderer.factory.get(), target.get());
                winrt::check_hresult(target->EndDraw());
                drawn = true;
            }
            catch (...)
            {
                renderer.target = nullptr;
                LogException("toolbar icon");
            }
            if (old && !SelectObject(renderer.dc, old))
            {
                LogError("SelectObject (restore toolbar icon DC) failed");
                renderer.target = nullptr;
                if (!DeleteDC(renderer.dc)) LogError("DeleteDC (toolbar icon) failed");
                renderer.dc = nullptr;
                drawn = false;
            }
            if (drawn) return bitmap;
            if (!DeleteObject(bitmap)) LogError("DeleteObject (failed toolbar icon) failed");
            return nullptr;
        }

        D2D1_COLOR_F ToD2D(Color c) { return D2D1::ColorF(c.r / 255.f, c.g / 255.f, c.b / 255.f, c.a / 255.f); }
    }

    HBITMAP PathIcon(std::wstring_view text, float pathSize, Color ink, int size, std::optional<Color> bar)
    {
        auto path = ParsePath(text);
        if (!path)
        {
            LogError("toolbar: an app button's path data does not parse");
            return nullptr;
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
            float iconSize = static_cast<float>(bar ? size * 3 / 4 : size);
            target->SetTransform(D2D1::Matrix3x2F::Scale(iconSize / pathSize, iconSize / pathSize) *
                D2D1::Matrix3x2F::Translation((static_cast<float>(size) - iconSize) / 2, 0));
            target->FillGeometry(geometry.get(), brush.get());
            if (bar)
            {
                target->SetTransform(D2D1::Matrix3x2F::Identity());
                float left = static_cast<float>(size / 8), right = static_cast<float>(size) - left;
                float top = static_cast<float>(size - size / 4), bottom = static_cast<float>(size - 1);
                brush->SetColor(D2D1::ColorF(0x808080));
                target->FillRectangle({ left, top, right, bottom }, brush.get());
                brush->SetColor(ToD2D(*bar));
                target->FillRectangle({ left + 1, top + 1, right - 1, bottom - 1 }, brush.get());
            }
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
