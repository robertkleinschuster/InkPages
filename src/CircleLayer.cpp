#include "CircleLayer.h"

#include <d2d1_1.h>
#include <d2d1_1helper.h>

#include <cmath>

#include "Graphics.h"
#include "inkpages/Log.h"

namespace inkpages::detail
{
    namespace
    {
        constexpr float Pad = 2; // pixels around the ring
    }

    CircleLayer::CircleLayer(Graphics& graphics) : graphics(graphics)
    {
        winrt::check_hresult(graphics.dcomp->CreateVisual(visual.put()));
    }

    void CircleLayer::Show(PointF at, float r, float renderScale)
    {
        if (r != radius || renderScale != scale || !surface)
        {
            radius = r;
            scale = renderScale;
            Draw();
        }
        float offset = radius + Pad / scale;
        D2D1_MATRIX_3X2_F m{ 1 / scale, 0, 0, 1 / scale, at.x - offset, at.y - offset };
        winrt::check_hresult(visual->SetTransform(m));
        if (!shown)
        {
            winrt::check_hresult(visual->SetContent(surface.get()));
            shown = true;
        }
    }

    void CircleLayer::Hide()
    {
        if (!shown) return;
        winrt::check_hresult(visual->SetContent(nullptr));
        shown = false;
    }

    void CircleLayer::Draw()
    {
        UINT size = static_cast<UINT>(std::ceil(2 * radius * scale + 2 * Pad));
        surface = nullptr;
        winrt::check_hresult(graphics.dcomp->CreateSurface(size, size, DXGI_FORMAT_B8G8R8A8_UNORM,
            DXGI_ALPHA_MODE_PREMULTIPLIED, surface.put()));
        winrt::com_ptr<ID2D1DeviceContext> dc;
        POINT offset{};
        winrt::check_hresult(surface->BeginDraw(nullptr, __uuidof(ID2D1DeviceContext), dc.put_void(), &offset));
        try
        {
            dc->SetTransform(D2D1::Matrix3x2F::Translation(static_cast<float>(offset.x), static_cast<float>(offset.y)));
            dc->Clear(D2D1::ColorF(0, 0, 0, 0));
            winrt::com_ptr<ID2D1SolidColorBrush> brush;
            winrt::check_hresult(dc->CreateSolidColorBrush(D2D1::ColorF(0.41f, 0.41f, 0.41f, 1), brush.put())); // dim gray
            float c = size / 2.f, ring = radius * scale - 0.5f; // the ring's outer edge is the radius
            dc->DrawEllipse(D2D1::Ellipse({ c, c }, ring, ring), brush.get(), 1);
        }
        catch (...)
        {
            LogException("eraser circle draw");
        }
        winrt::check_hresult(surface->EndDraw());
        if (shown) winrt::check_hresult(visual->SetContent(surface.get()));
    }
}
