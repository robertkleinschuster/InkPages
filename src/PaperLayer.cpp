#include "PaperLayer.h"

#include <d2d1_1.h>
#include <d2d1_1helper.h>

#include <algorithm>
#include <cmath>
#include <format>

#include "Graphics.h"
#include "inkpages/Log.h"

namespace inkpages::detail
{
    namespace
    {
        D2D1_COLOR_F ToD2D(Color c) { return { c.r / 255.f, c.g / 255.f, c.b / 255.f, c.a / 255.f }; }
    }

    PaperLayer::PaperLayer(Graphics& graphics, RectF page, Paper paper) : graphics(graphics), page(page), paper(paper)
    {
        winrt::check_hresult(graphics.dcomp->CreateVisual(visual.put()));
        winrt::check_hresult(graphics.dcomp->CreateVirtualSurface(1, 1, DXGI_FORMAT_B8G8R8A8_UNORM,
            DXGI_ALPHA_MODE_PREMULTIPLIED, surface.put()));
        winrt::check_hresult(visual->SetContent(surface.get()));
        winrt::check_hresult(visual->SetBitmapInterpolationMode(DCOMPOSITION_BITMAP_INTERPOLATION_MODE_LINEAR));
    }

    bool PaperLayer::Update(RectF needed, float renderScale)
    {
        // The part of the sheet to have drawn, page-relative.
        RectF want{ std::max(0.f, needed.left - page.left), std::max(0.f, needed.top - page.top),
            std::min(page.Width(), needed.right - page.left), std::min(page.Height(), needed.bottom - page.top) };
        if (want.right <= want.left || want.bottom <= want.top) return false;
        bool newScale = renderScale != scale;
        if (!newScale && want.left >= drawn.left && want.top >= drawn.top && want.right <= drawn.right && want.bottom <= drawn.bottom)
        {
            return false;
        }
        if (newScale)
        {
            UINT w = static_cast<UINT>(std::ceil(page.Width() * renderScale));
            UINT h = static_cast<UINT>(std::ceil(page.Height() * renderScale));
            winrt::check_hresult(surface->Resize(w, h));
            D2D1_MATRIX_3X2_F m{ 1 / renderScale, 0, 0, 1 / renderScale, page.left, page.top };
            winrt::check_hresult(visual->SetTransform(m));
            scale = renderScale;
            drawn = {};
        }
        // Draw the whole wanted area (it may overlap what is drawn; the sheet is cheap to draw) and keep only it.
        RECT px{ static_cast<LONG>(std::floor(want.left * scale)), static_cast<LONG>(std::floor(want.top * scale)),
            static_cast<LONG>(std::ceil(want.right * scale)), static_cast<LONG>(std::ceil(want.bottom * scale)) };
        Draw(px);
        winrt::check_hresult(surface->Trim(&px, 1));
        drawn = want;
        return true;
    }

    void PaperLayer::Discard()
    {
        if (drawn.right <= drawn.left) return;
        winrt::check_hresult(surface->Trim(nullptr, 0));
        drawn = {};
    }

    void PaperLayer::Draw(RECT px)
    {
        winrt::com_ptr<ID2D1DeviceContext> dc;
        POINT offset{};
        winrt::check_hresult(surface->BeginDraw(&px, __uuidof(ID2D1DeviceContext), dc.put_void(), &offset));
        try
        {
            // BeginDraw's offset is where px's top-left lies in the context; draw in page DIPs.
            dc->SetTransform(D2D1::Matrix3x2F::Scale(scale, scale) *
                D2D1::Matrix3x2F::Translation(static_cast<float>(offset.x - px.left), static_cast<float>(offset.y - px.top)));
            D2D1_RECT_F clip{ px.left / scale, px.top / scale, px.right / scale, px.bottom / scale };
            dc->PushAxisAlignedClip(clip, D2D1_ANTIALIAS_MODE_ALIASED);
            dc->Clear(ToD2D(paper.sheetColor));
            winrt::com_ptr<ID2D1SolidColorBrush> brush;
            winrt::check_hresult(dc->CreateSolidColorBrush(ToD2D(paper.lineColor), brush.put()));
            for (Line const& line : paper.Lines(page.Width(), page.Height()))
            {
                bool vertical = line.from.x == line.to.x;
                if (vertical ? (line.from.x < clip.left - 2 || line.from.x > clip.right + 2)
                             : (line.from.y < clip.top - 2 || line.from.y > clip.bottom + 2))
                {
                    continue;
                }
                dc->DrawLine({ line.from.x, line.from.y }, { line.to.x, line.to.y }, brush.get(), paper.thickness);
            }
            dc->PopAxisAlignedClip();
        }
        catch (...)
        {
            LogException("paper draw");
        }
        winrt::check_hresult(surface->EndDraw());
    }
}
