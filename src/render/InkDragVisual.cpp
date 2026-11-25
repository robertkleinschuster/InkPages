#include "render/InkDragVisual.h"

#include <algorithm>
#include <cmath>
#include <inkrenderer.h>
#include <winrt/Windows.Foundation.Collections.h>

#include "inkpages/Log.h"
#include "render/Graphics.h"

namespace inkpages::detail
{
    namespace
    {
        winrt::com_ptr<IDCompositionVisual> Draw(Graphics& graphics, IInkD2DRenderer2& renderer,
            std::vector<InkDragSnapshot::Stroke> strokes, float scale, float maxSide)
        {
            auto first = strokes.front().BoundingRect();
            RectF bounds{ first.X, first.Y, first.X + first.Width, first.Y + first.Height };
            for (auto const& stroke : strokes)
            {
                auto r = stroke.BoundingRect();
                bounds = { std::min(bounds.left, r.X), std::min(bounds.top, r.Y),
                    std::max(bounds.right, r.X + r.Width), std::max(bounds.bottom, r.Y + r.Height) };
            }
            bounds = bounds.Inflated(2, 2);
            scale = std::min(scale, maxSide / std::max(bounds.Width(), bounds.Height()));
            auto width = static_cast<UINT>(std::ceil(bounds.Width() * scale));
            auto height = static_cast<UINT>(std::ceil(bounds.Height() * scale));
            winrt::com_ptr<IDCompositionSurface> surface;
            winrt::check_hresult(graphics.dcomp->CreateSurface(width, height, DXGI_FORMAT_B8G8R8A8_UNORM,
                DXGI_ALPHA_MODE_PREMULTIPLIED, surface.put()));
            auto collection = winrt::single_threaded_vector(std::move(strokes));
            winrt::com_ptr<ID2D1DeviceContext> dc;
            POINT origin{};
            winrt::check_hresult(surface->BeginDraw(nullptr, __uuidof(ID2D1DeviceContext), dc.put_void(), &origin));
            dc->Clear(D2D1::ColorF(0, 0, 0, 0));
            dc->SetTransform(D2D1::Matrix3x2F(scale, 0, 0, scale,
                static_cast<float>(origin.x) - bounds.left * scale, static_cast<float>(origin.y) - bounds.top * scale));
            HRESULT draw = renderer.Draw(dc.get(), collection.as<::IUnknown>().get(), USE_SYSTEM_COLORS_WHEN_NECESSARY);
            winrt::check_hresult(surface->EndDraw());
            winrt::check_hresult(draw);

            winrt::com_ptr<IDCompositionVisual> visual;
            winrt::check_hresult(graphics.dcomp->CreateVisual(visual.put()));
            winrt::check_hresult(visual->SetContent(surface.get()));
            D2D1_MATRIX_3X2_F transform{ 1 / scale, 0, 0, 1 / scale, bounds.left, bounds.top };
            winrt::check_hresult(visual->SetTransform(transform));
            return visual;
        }

        // The objects (document units) in a texture of their own; the visual sits at the bounds' top left.
        winrt::com_ptr<IDCompositionVisual> DrawObjects(Graphics& graphics, InkDragObjects const& objects, float scale, float maxSide)
        {
            RectF bounds = objects.bounds.Inflated(2, 2);
            scale = std::min(scale, maxSide / std::max(bounds.Width(), bounds.Height()));
            auto width = static_cast<UINT>(std::ceil(bounds.Width() * scale));
            auto height = static_cast<UINT>(std::ceil(bounds.Height() * scale));
            winrt::com_ptr<IDCompositionSurface> surface;
            winrt::check_hresult(graphics.dcomp->CreateSurface(width, height, DXGI_FORMAT_B8G8R8A8_UNORM,
                DXGI_ALPHA_MODE_PREMULTIPLIED, surface.put()));
            winrt::com_ptr<ID2D1DeviceContext> dc;
            POINT origin{};
            winrt::check_hresult(surface->BeginDraw(nullptr, __uuidof(ID2D1DeviceContext), dc.put_void(), &origin));
            dc->Clear(D2D1::ColorF(0, 0, 0, 0));
            dc->SetTransform(D2D1::Matrix3x2F(scale, 0, 0, scale,
                static_cast<float>(origin.x) - bounds.left * scale, static_cast<float>(origin.y) - bounds.top * scale));
            bool drawn = false;
            try { drawn = objects.draw(dc.get()); }
            catch (...) { LogException("selection: draw the objects of the drag preview"); }
            winrt::check_hresult(surface->EndDraw());
            if (!drawn) throw winrt::hresult_error(E_FAIL, L"the objects cannot be drawn for the drag preview");

            winrt::com_ptr<IDCompositionVisual> visual;
            winrt::check_hresult(graphics.dcomp->CreateVisual(visual.put()));
            winrt::check_hresult(visual->SetContent(surface.get()));
            D2D1_MATRIX_3X2_F transform{ 1 / scale, 0, 0, 1 / scale, bounds.left, bounds.top };
            winrt::check_hresult(visual->SetTransform(transform));
            return visual;
        }
    }

    InkDragVisual::InkDragVisual(Graphics& graphics, IDCompositionVisual* parent, IDCompositionVisual* liveInk)
        : graphics(graphics)
    {
        winrt::com_ptr<IDCompositionVisual> group;
        winrt::check_hresult(graphics.dcomp->CreateVisual(group.put()));
        preview = group.as<IDCompositionVisual3>();
        winrt::check_hresult(liveInk->QueryInterface(__uuidof(IDCompositionVisual3), live.put_void()));
        winrt::check_hresult(preview->SetVisible(FALSE));
        winrt::check_hresult(parent->AddVisual(group.get(), FALSE, nullptr));
        // The objects (pictures, texts) lie below the ink, as on the pages: their group sits right below the live ink
        // (and so below the composited ink in `preview`, above it).
        winrt::check_hresult(graphics.dcomp->CreateVisual(objectsGroup.put()));
        winrt::check_hresult(parent->AddVisual(objectsGroup.get(), FALSE, liveInk));
    }

    void InkDragVisual::Prepare(InkDragSnapshot snapshot, std::optional<InkDragObjects> objects, float scale)
    {
        Reset();
        if ((snapshot.moving.empty() && !objects) || scale <= 0) return;
        try
        {
            // Bound the aggregate texture allocation even when several pages have live ink.
            float maxSide = 4095 / std::sqrt(static_cast<float>(snapshot.stationary.size() + 2));
            winrt::check_hresult(graphics.dcomp->CreateVisual(movingObjects.put()));
            winrt::check_hresult(objectsGroup->AddVisual(movingObjects.get(), FALSE, nullptr));
            if (objects)
            {
                auto visual = DrawObjects(graphics, *objects, scale, maxSide);
                winrt::check_hresult(movingObjects->AddVisual(visual.get(), FALSE, nullptr));
            }
            hidesLive = !snapshot.moving.empty();
            if (hidesLive)
            {
                auto renderer = winrt::create_instance<IInkD2DRenderer2>(__uuidof(InkD2DRenderer));
                for (auto& entry : snapshot.stationary)
                {
                    auto visual = Draw(graphics, *renderer.get(), std::move(entry.second), scale, maxSide);
                    winrt::check_hresult(preview->AddVisual(visual.get(), FALSE, nullptr));
                }
                winrt::check_hresult(graphics.dcomp->CreateVisual(moving.put()));
                auto visual = Draw(graphics, *renderer.get(), std::move(snapshot.moving), scale, maxSide);
                winrt::check_hresult(moving->AddVisual(visual.get(), FALSE, nullptr));
                winrt::check_hresult(preview->AddVisual(moving.get(), FALSE, nullptr));
            }
            ready = true;
            Log("selection: composited drag preview ready");
        }
        catch (...)
        {
            LogException("selection: prepare composited drag preview");
            Reset();
        }
    }

    bool InkDragVisual::Move(Affine const& m)
    {
        if (!ready) return false;
        D2D_MATRIX_3X2_F matrix{ m.m11, m.m12, m.m21, m.m22, m.dx, m.dy };
        winrt::check_hresult(movingObjects->SetTransform(matrix));
        if (moving) winrt::check_hresult(moving->SetTransform(matrix));
        if (!active)
        {
            winrt::check_hresult(preview->SetVisible(TRUE));
            if (hidesLive) winrt::check_hresult(live->SetVisible(FALSE));
            active = true;
            Log("selection: moving composited ink preview");
        }
        return true;
    }

    void InkDragVisual::Reset()
    {
        ready = false;
        if (active)
        {
            if (hidesLive) winrt::check_hresult(live->SetVisible(TRUE));
            winrt::check_hresult(preview->SetVisible(FALSE));
            active = false;
        }
        moving = nullptr;
        winrt::check_hresult(objectsGroup->RemoveAllVisuals());
        movingObjects = nullptr;
        hidesLive = false;
        winrt::check_hresult(preview->RemoveAllVisuals());
    }
}
