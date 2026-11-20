#include "inkpages/model/ViewMath.h"

#include <algorithm>

namespace inkpages
{
    float ClampZoom(float zoom, float minZoom, float maxZoom)
    {
        return std::clamp(zoom, minZoom, maxZoom);
    }

    float FitWidthZoom(float documentWidth, float viewportWidthPx, float scale)
    {
        return documentWidth > 0 && scale > 0 ? viewportWidthPx / (documentWidth * scale) : 1;
    }

    PointF DocumentToViewport(PointF document, View view, float scale)
    {
        return { document.x * scale * view.zoom + view.dx, document.y * scale * view.zoom + view.dy };
    }

    PointF ViewportToDocument(PointF viewport, View view, float scale)
    {
        float f = scale * view.zoom;
        return { (viewport.x - view.dx) / f, (viewport.y - view.dy) / f };
    }

    RectF VisibleDocument(SizeF viewportPx, View view, float scale)
    {
        PointF a = ViewportToDocument({ 0, 0 }, view, scale);
        PointF b = ViewportToDocument({ viewportPx.width, viewportPx.height }, view, scale);
        return { a.x, a.y, b.x, b.y };
    }

    RectF ZoomRect(SizeF viewportPx, View view, float zoom, PointF anchorPx)
    {
        // Content pixels under the anchor stay under it: content = (viewport - offset) / zoom.
        float cx = (anchorPx.x - view.dx) / view.zoom, cy = (anchorPx.y - view.dy) / view.zoom;
        float left = cx - anchorPx.x / zoom, top = cy - anchorPx.y / zoom;
        return { left, top, left + viewportPx.width / zoom, top + viewportPx.height / zoom };
    }

    RectF KeepOnContent(RectF rect, SizeF content)
    {
        float w = rect.Width(), h = rect.Height();
        float left = w >= content.width ? (content.width - w) / 2 : std::clamp(rect.left, 0.f, content.width - w);
        float top = h >= content.height ? 0 : std::clamp(rect.top, 0.f, content.height - h);
        return { left, top, left + w, top + h };
    }

    RectF ScrollRect(SizeF viewportPx, View view, float scale, float documentTop)
    {
        float left = -view.dx / view.zoom, top = documentTop * scale;
        return { left, top, left + viewportPx.width / view.zoom, top + viewportPx.height / view.zoom };
    }

    RectF ScrollToPointRect(SizeF viewportPx, View view, float scale, PointF at, float marginPx)
    {
        float m = marginPx / view.zoom; // content pixels
        float left = at.x * scale - m, top = at.y * scale - m;
        return { left, top, left + viewportPx.width / view.zoom, top + viewportPx.height / view.zoom };
    }

    std::optional<RectF> RevealRect(SizeF viewportPx, View view, float scale, RectF area, float marginPx)
    {
        float m = marginPx / view.zoom;
        RectF shown{ -view.dx / view.zoom, -view.dy / view.zoom, (viewportPx.width - view.dx) / view.zoom,
            (viewportPx.height - view.dy) / view.zoom };
        RectF want{ area.left * scale - m, area.top * scale - m, area.right * scale + m, area.bottom * scale + m };
        // Per axis: the least shift that shows the area; an area larger than the view shows its start.
        auto shift = [](float lo, float hi, float viewLo, float viewHi) {
            if (lo < viewLo || hi - lo > viewHi - viewLo) return lo - viewLo;
            if (hi > viewHi) return hi - viewHi;
            return 0.f;
        };
        float dx = shift(want.left, want.right, shown.left, shown.right);
        float dy = shift(want.top, want.bottom, shown.top, shown.bottom);
        if (dx == 0 && dy == 0) return std::nullopt;
        return RectF{ shown.left + dx, shown.top + dy, shown.right + dx, shown.bottom + dy };
    }

    RectF PanRect(SizeF viewportPx, View view, float dxPx, float dyPx)
    {
        float left = (dxPx - view.dx) / view.zoom, top = (dyPx - view.dy) / view.zoom;
        return { left, top, left + viewportPx.width / view.zoom, top + viewportPx.height / view.zoom };
    }

    SizeF ViewportWithInputPane(SizeF client, RectF pane)
    {
        if (pane.top > 0 && pane.top < client.height && pane.left <= 0 &&
            pane.right >= client.width && pane.bottom >= client.height)
            client.height = pane.top;
        return client;
    }

    PointF CaretScroll(SizeF viewport, RectF caret, RectF pane, float margin)
    {
        RectF wanted = caret.Inflated(margin, margin);
        auto shift = [](float start, float end, float edge) {
            if (start < 0) return start;
            return std::max(0.f, end - edge);
        };
        PointF scroll{ shift(wanted.left, wanted.right, viewport.width),
            shift(wanted.top, wanted.bottom, viewport.height) };
        RectF shifted{ wanted.left - scroll.x, wanted.top - scroll.y, wanted.right - scroll.x, wanted.bottom - scroll.y };
        if (shifted.Intersects(pane))
        {
            if (pane.top >= shifted.Height()) scroll.y += shifted.bottom - pane.top;
            else if (viewport.height - pane.bottom >= shifted.Height()) scroll.y += shifted.top - pane.bottom;
        }
        return scroll;
    }

    RectF KeyScrollRect(ScrollKey key, SizeF viewportPx, View view, float scale, float documentHeight, float linePx,
        float overlapPx)
    {
        float page = std::max(linePx, viewportPx.height - overlapPx);
        switch (key)
        {
        case ScrollKey::LineUp: return PanRect(viewportPx, view, 0, -linePx);
        case ScrollKey::LineDown: return PanRect(viewportPx, view, 0, linePx);
        case ScrollKey::LineLeft: return PanRect(viewportPx, view, -linePx, 0);
        case ScrollKey::LineRight: return PanRect(viewportPx, view, linePx, 0);
        case ScrollKey::PageUp: return PanRect(viewportPx, view, 0, -page);
        case ScrollKey::PageDown: return PanRect(viewportPx, view, 0, page);
        case ScrollKey::Start: return ScrollRect(viewportPx, view, scale, 0);
        case ScrollKey::End:
        {
            RectF r = ScrollRect(viewportPx, view, scale, 0);
            float bottom = documentHeight * scale;
            return { r.left, bottom - r.Height(), r.right, bottom };
        }
        }
        return PanRect(viewportPx, view, 0, 0);
    }

    RectF FitPageRect(RectF page, SizeF viewportPx, float scale, float marginDip, float minZoom, float maxZoom)
    {
        float w = (page.Width() + 2 * marginDip) * scale, h = (page.Height() + 2 * marginDip) * scale;
        float zoom = w > 0 && h > 0 ? std::min(viewportPx.width / w, viewportPx.height / h) : 1;
        zoom = ClampZoom(zoom, minZoom, maxZoom);
        float cx = (page.left + page.right) / 2 * scale, cy = (page.top + page.bottom) / 2 * scale;
        float rw = viewportPx.width / zoom, rh = viewportPx.height / zoom;
        return { cx - rw / 2, cy - rh / 2, cx + rw / 2, cy + rh / 2 };
    }

    std::optional<ViewPlace> PlaceOf(PageLayout const& layout, RectF visible, float zoom)
    {
        auto page = layout.PageInView(visible); // the page shown most, as the app's "Seite n" counts it
        if (!page) return std::nullopt;
        RectF r = layout.Page(*page);
        return ViewPlace{ *page, { visible.left - r.left, visible.top - r.top }, zoom };
    }

    RectF PlaceRect(PageLayout const& layout, ViewPlace place, SizeF viewportPx, float scale, float minZoom, float maxZoom)
    {
        float zoom = ClampZoom(place.zoom, minZoom, maxZoom);
        PointF origin;
        if (layout.Count() > 0)
        {
            RectF r = layout.Page(std::clamp(place.page, 0, layout.Count() - 1));
            origin = { r.left, r.top };
        }
        float left = (origin.x + place.point.x) * scale, top = (origin.y + place.point.y) * scale;
        return { left, top, left + viewportPx.width / zoom, top + viewportPx.height / zoom };
    }

    std::set<int> LivePages(PageLayout const& layout, RectF visible, std::set<int> const& live, std::set<int> const& held)
    {
        float h = visible.Height();
        std::set<int> result(held.begin(), held.end());
        RectF near{ -1e9f, visible.top - h, 1e9f, visible.bottom + h };
        for (int page : layout.PagesIn(near))
        {
            result.insert(page);
        }
        RectF keep{ -1e9f, visible.top - 2 * h, 1e9f, visible.bottom + 2 * h };
        for (int page : live)
        {
            if (page < layout.Count() && layout.Page(page).Intersects(keep))
            {
                result.insert(page);
            }
        }
        std::erase_if(result, [&](int page) { return page < 0 || page >= layout.Count(); });
        return result;
    }
}
