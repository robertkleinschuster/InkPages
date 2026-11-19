#include "render/PageMark.h"

#include <d2d1_1.h>
#include <d2d1_1helper.h>

#include <cmath>

#include "render/Graphics.h"

namespace inkpages::detail
{
    PageMark::PageMark(Graphics& graphics, IDCompositionVisual* parent) : graphics(graphics)
    {
        winrt::check_hresult(graphics.dcomp->CreateVisual(visual.put()));
        for (auto& strip : strips)
        {
            winrt::check_hresult(graphics.dcomp->CreateVisual(strip.put()));
            winrt::check_hresult(visual->AddVisual(strip.get(), FALSE, nullptr));
        }
        winrt::check_hresult(parent->AddVisual(visual.get(), FALSE, nullptr));
    }

    void PageMark::Show(std::optional<RectF> page, Color color, float scale)
    {
        if (page == shown && color == shownColor && scale == shownScale) return;
        shown = page;
        shownColor = color;
        shownScale = scale;
        if (!page || scale <= 0)
        {
            for (auto& strip : strips) winrt::check_hresult(strip->SetContent(nullptr));
            return;
        }
        RectF o = page->Inflated(Gap + Thickness, Gap + Thickness); // the outline's outer edge
        float band = Thickness + Radius;                              // top and bottom strips hold the corners
        DrawStrip(0, { o.left, o.top, o.right, o.top + band }, o, color, scale);
        DrawStrip(1, { o.left, o.bottom - band, o.right, o.bottom }, o, color, scale);
        DrawStrip(2, { o.left, o.top + band, o.left + Thickness, o.bottom - band }, o, color, scale);
        DrawStrip(3, { o.right - Thickness, o.top + band, o.right, o.bottom - band }, o, color, scale);
    }

    // One strip: the whole rounded outline drawn through the strip's window, at `scale` pixels per page unit.
    void PageMark::DrawStrip(int index, RectF strip, RectF outline, Color color, float scale)
    {
        UINT w = static_cast<UINT>(std::ceil(strip.Width() * scale)), h = static_cast<UINT>(std::ceil(strip.Height() * scale));
        auto& v = strips[index];
        if (w == 0 || h == 0)
        {
            winrt::check_hresult(v->SetContent(nullptr));
            return;
        }
        winrt::com_ptr<IDCompositionSurface> surface;
        winrt::check_hresult(graphics.dcomp->CreateSurface(w, h, DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_ALPHA_MODE_PREMULTIPLIED, surface.put()));
        winrt::com_ptr<ID2D1DeviceContext> dc;
        POINT offset{};
        winrt::check_hresult(surface->BeginDraw(nullptr, __uuidof(ID2D1DeviceContext), dc.put_void(), &offset));
        dc->SetTransform(D2D1::Matrix3x2F::Translation(-strip.left, -strip.top) * D2D1::Matrix3x2F::Scale(scale, scale) *
            D2D1::Matrix3x2F::Translation(static_cast<float>(offset.x), static_cast<float>(offset.y)));
        dc->Clear(D2D1::ColorF(0, 0, 0, 0));
        winrt::com_ptr<ID2D1SolidColorBrush> brush;
        winrt::check_hresult(dc->CreateSolidColorBrush(D2D1::ColorF(color.r / 255.f, color.g / 255.f, color.b / 255.f, color.a / 255.f), brush.put()));
        float half = Thickness / 2; // the stroke's middle: the border lies inside the outer edge, as a XAML Border's
        D2D1_ROUNDED_RECT r{ { outline.left + half, outline.top + half, outline.right - half, outline.bottom - half },
            Radius - half, Radius - half };
        dc->DrawRoundedRectangle(r, brush.get(), Thickness);
        winrt::check_hresult(surface->EndDraw());
        winrt::check_hresult(v->SetContent(surface.get()));
        D2D1_MATRIX_3X2_F m{ 1 / scale, 0, 0, 1 / scale, strip.left, strip.top };
        winrt::check_hresult(v->SetTransform(m));
    }
}
