#include "render/Overlay.h"

#include <Windows.h>
#include <d2d1_1.h>
#include <d2d1_1helper.h>

#include <algorithm>
#include <cmath>

#include "inkpages/Log.h"
#include "render/Graphics.h"

namespace inkpages::detail
{
    template <typename F> void OverlayModel::Change(F&& f)
    {
        {
            std::scoped_lock guard(lock);
            f(content);
        }
        if (changed) changed();
    }

    void OverlayModel::SetEraser(std::optional<PointF> at, float extent, bool square)
    {
        Change([&](OverlayContent& c) { c.circleAt = at; c.circleRadius = extent; c.squareEraser = square; });
    }

    void OverlayModel::SetLasso(std::vector<PointF> points)
    {
        Change([&](OverlayContent& c) { c.lasso = std::move(points); });
    }

    void OverlayModel::SetFrame(std::optional<SelectionFrame> frame)
    {
        Change([&](OverlayContent& c) { c.frame = frame; });
    }

    void OverlayModel::SetShape(std::vector<PointF> points, Color color, float width)
    {
        Change([&](OverlayContent& c) { c.shape = std::move(points); c.shapeColor = color; c.shapeWidth = width; });
    }

    PointF OverlayModel::AddLaser(PointF from, std::vector<PointF> const& points, float width, int seconds)
    {
        auto duration = static_cast<uint64_t>(std::clamp(seconds, 1, 10)) * 1000;
        auto expires = GetTickCount64() + duration;
        Change([&](OverlayContent& c) {
            for (auto to : points)
            {
                c.laser.push_back({ from, to, width, expires, duration });
                from = to;
            }
        });
        return from;
    }

    void OverlayModel::ClearLaser()
    {
        Change([](OverlayContent& c) { c.laser.clear(); });
    }

    OverlayContent OverlayModel::Get()
    {
        std::scoped_lock guard(lock);
        content.now = GetTickCount64();
        std::erase_if(content.laser, [&](LaserSegment const& segment) { return segment.expires <= content.now; });
        return content;
    }

    OverlayLayer::OverlayLayer(Graphics& graphics) : graphics(graphics)
    {
        winrt::check_hresult(graphics.dcomp->CreateVisual(visual.put()));
    }

    namespace
    {
        constexpr D2D1_COLOR_F Gray{ 0.41f, 0.41f, 0.41f, 1 };
        constexpr D2D1_COLOR_F Accent{ 0.0f, 0.37f, 0.72f, 1 }; // Windows' default accent blue
        D2D1_COLOR_F ToD2D(Color c) { return { c.r / 255.f, c.g / 255.f, c.b / 255.f, c.a / 255.f }; }

        void Grow(RectF& r, bool& any, PointF p, float pad)
        {
            RectF q{ p.x - pad, p.y - pad, p.x + pad, p.y + pad };
            if (!any) { r = q; any = true; return; }
            r = { std::min(r.left, q.left), std::min(r.top, q.top), std::max(r.right, q.right), std::max(r.bottom, q.bottom) };
        }

        void Polyline(ID2D1DeviceContext* dc, std::vector<PointF> const& points, ID2D1Brush* brush, float width,
            ID2D1StrokeStyle* style, bool close)
        {
            for (size_t i = 1; i < points.size(); ++i)
            {
                dc->DrawLine({ points[i - 1].x, points[i - 1].y }, { points[i].x, points[i].y }, brush, width, style);
            }
            if (close && points.size() > 2)
            {
                dc->DrawLine({ points.back().x, points.back().y }, { points.front().x, points.front().y }, brush, width, style);
            }
        }
    }

    void OverlayLayer::Draw(OverlayContent const& c, float scale)
    {
        bool frameOnly = c.frame && !c.circleAt && c.lasso.empty() && c.shape.empty() && c.laser.empty();
        if (frameOnly && drawnFrame && frameRenderScale == scale &&
            drawnFrame->bounds == c.frame->bounds && drawnFrame->margin == c.frame->margin &&
            drawnFrame->handle == c.frame->handle)
        {
            auto previous = drawnFrame->orientation;
            auto current = c.frame->orientation;
            float dx = current.dx - previous.dx, dy = current.dy - previous.dy;
            previous.dx = current.dx;
            previous.dy = current.dy;
            if (previous == current)
            {
                // A translation only moves the existing DirectComposition surface.
                D2D1_MATRIX_3X2_F m{ 1 / drawnScale, 0, 0, 1 / drawnScale, drawnOrigin.x + dx, drawnOrigin.y + dy };
                winrt::check_hresult(visual->SetTransform(m));
                return;
            }
        }
        drawnFrame.reset();
        RectF bounds{};
        bool any = false;
        if (c.circleAt) Grow(bounds, any, *c.circleAt, c.circleRadius + 1);
        for (PointF p : c.lasso) Grow(bounds, any, p, 1);
        if (c.frame)
        {
            RectF f = c.frame->orientation.Bounds(c.frame->Frame());
            float pad = c.frame->handle;
            Grow(bounds, any, { f.left, f.top }, pad);
            Grow(bounds, any, { f.right, f.bottom }, pad);
            for (Handle h : { Handle::Rotate, Handle::Delete, Handle::Duplicate })
            {
                RectF r = c.frame->orientation.Bounds(c.frame->HandleRect(h));
                Grow(bounds, any, { r.left, r.top }, pad);
                Grow(bounds, any, { r.right, r.bottom }, pad);
            }
        }
        for (PointF p : c.shape) Grow(bounds, any, p, c.shapeWidth + 2);
        for (auto const& segment : c.laser)
        {
            Grow(bounds, any, segment.from, segment.width + 1);
            Grow(bounds, any, segment.to, segment.width + 1);
        }
        if (!any)
        {
            winrt::check_hresult(visual->SetContent(nullptr));
            surface = nullptr;
            return;
        }
        // At most the size of a large screen in pixels: beyond that the overlay is drawn coarser (it is feedback).
        float pixels = std::max(bounds.Width(), bounds.Height()) * scale;
        float s = pixels > 4096 ? scale * 4096 / pixels : scale;
        UINT w = static_cast<UINT>(std::ceil(bounds.Width() * s)) + 1, h = static_cast<UINT>(std::ceil(bounds.Height() * s)) + 1;
        surface = nullptr;
        winrt::check_hresult(graphics.dcomp->CreateSurface(w, h, DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_ALPHA_MODE_PREMULTIPLIED, surface.put()));
        winrt::com_ptr<ID2D1DeviceContext> dc;
        POINT offset{};
        winrt::check_hresult(surface->BeginDraw(nullptr, __uuidof(ID2D1DeviceContext), dc.put_void(), &offset));
        try
        {
            dc->SetTransform(D2D1::Matrix3x2F::Translation(-bounds.left, -bounds.top) * D2D1::Matrix3x2F::Scale(s, s) *
                D2D1::Matrix3x2F::Translation(static_cast<float>(offset.x), static_cast<float>(offset.y)));
            dc->Clear(D2D1::ColorF(0, 0, 0, 0));
            winrt::com_ptr<ID2D1SolidColorBrush> gray, accent, white;
            winrt::check_hresult(dc->CreateSolidColorBrush(Gray, gray.put()));
            winrt::check_hresult(dc->CreateSolidColorBrush(Accent, accent.put()));
            winrt::check_hresult(dc->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 1), white.put()));
            float hair = 1 / s; // one pixel
            if (c.circleAt) // the ring's outer edge is the radius
            {
                auto [x, y] = *c.circleAt;
                float extent = std::max(0.f, c.circleRadius - hair / 2);
                if (c.squareEraser)
                    dc->DrawRectangle(D2D1::RectF(x - extent, y - extent, x + extent, y + extent), gray.get(), hair);
                else
                    dc->DrawEllipse(D2D1::Ellipse({ x, y }, extent, extent), gray.get(), hair);
            }
            if (!c.lasso.empty())
            {
                winrt::com_ptr<ID2D1StrokeStyle> dashed;
                D2D1_STROKE_STYLE_PROPERTIES dash = D2D1::StrokeStyleProperties();
                dash.dashStyle = D2D1_DASH_STYLE_DASH;
                winrt::check_hresult(graphics.d2dFactory->CreateStrokeStyle(dash, nullptr, 0, dashed.put()));
                Polyline(dc.get(), c.lasso, accent.get(), hair, dashed.get(), true);
            }
            if (c.frame)
            {
                auto const& m = c.frame->orientation;
                D2D1::Matrix3x2F drawn;
                dc->GetTransform(&drawn);
                dc->SetTransform(D2D1::Matrix3x2F(m.m11, m.m12, m.m21, m.m22, m.dx, m.dy) * drawn);
                RectF f = c.frame->Frame();
                dc->DrawRectangle({ f.left, f.top, f.right, f.bottom }, accent.get(), hair);
                for (Handle handle : { Handle::TopLeft, Handle::TopRight, Handle::BottomRight, Handle::BottomLeft,
                    Handle::Rotate, Handle::Delete, Handle::Duplicate })
                {
                    RectF r = c.frame->HandleRect(handle);
                    D2D1_RECT_F rect{ r.left, r.top, r.right, r.bottom };
                    dc->FillRectangle(rect, white.get());
                    dc->DrawRectangle(rect, accent.get(), hair);
                    float x = (r.left + r.right) / 2, y = (r.top + r.bottom) / 2, a = r.Width() / 4;
                    if (handle == Handle::Delete)
                    {
                        dc->DrawLine({ x - a, y - a }, { x + a, y + a }, accent.get(), hair);
                        dc->DrawLine({ x + a, y - a }, { x - a, y + a }, accent.get(), hair);
                    }
                    else if (handle == Handle::Duplicate)
                    {
                        dc->DrawRectangle({ x - a / 3, y - a, x + a, y + a / 3 }, accent.get(), hair);
                        dc->FillRectangle({ x - a, y - a / 3, x + a / 3, y + a }, white.get());
                        dc->DrawRectangle({ x - a, y - a / 3, x + a / 3, y + a }, accent.get(), hair);
                    }
                    else if (handle == Handle::Rotate)
                    {
                        dc->DrawLine({ f.right, y }, { r.left, y }, accent.get(), hair);
                        dc->DrawEllipse(D2D1::Ellipse({ x, y }, a, a), accent.get(), hair);
                        dc->DrawLine({ x + a, y }, { x + a / 3, y - a / 2 }, accent.get(), hair);
                    }
                }
                dc->SetTransform(drawn);
            }
            if (!c.laser.empty())
            {
                winrt::com_ptr<ID2D1SolidColorBrush> red;
                winrt::check_hresult(dc->CreateSolidColorBrush(D2D1::ColorF(1.f, 0.12f, 0.17f), red.put()));
                auto caps = D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND);
                winrt::com_ptr<ID2D1StrokeStyle> rounded;
                winrt::check_hresult(graphics.d2dFactory->CreateStrokeStyle(caps, nullptr, 0, rounded.put()));
                for (auto const& segment : c.laser)
                {
                    red->SetOpacity(static_cast<float>(segment.expires - c.now) / static_cast<float>(segment.duration));
                    D2D1_POINT_2F from{ segment.from.x, segment.from.y }, to{ segment.to.x, segment.to.y };
                    if (from.x == to.x && from.y == to.y)
                        dc->FillEllipse(D2D1::Ellipse(to, segment.width / 2, segment.width / 2), red.get());
                    else dc->DrawLine(from, to, red.get(), segment.width, rounded.get());
                }
            }
            if (!c.shape.empty())
            {
                winrt::com_ptr<ID2D1SolidColorBrush> pen;
                winrt::check_hresult(dc->CreateSolidColorBrush(ToD2D(c.shapeColor), pen.put()));
                winrt::com_ptr<ID2D1StrokeStyle> round;
                D2D1_STROKE_STYLE_PROPERTIES props = D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
                    D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND);
                winrt::check_hresult(graphics.d2dFactory->CreateStrokeStyle(props, nullptr, 0, round.put()));
                Polyline(dc.get(), c.shape, pen.get(), c.shapeWidth, round.get(), false);
            }
        }
        catch (...)
        {
            LogException("overlay draw");
        }
        winrt::check_hresult(surface->EndDraw());
        D2D1_MATRIX_3X2_F m{ 1 / s, 0, 0, 1 / s, bounds.left, bounds.top };
        winrt::check_hresult(visual->SetTransform(m));
        winrt::check_hresult(visual->SetContent(surface.get()));
        if (frameOnly)
        {
            drawnFrame = c.frame;
            drawnOrigin = { bounds.left, bounds.top };
            drawnScale = s;
            frameRenderScale = scale;
        }
    }
}
