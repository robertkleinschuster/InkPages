#include "inkpages/ViewMath.h"

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
