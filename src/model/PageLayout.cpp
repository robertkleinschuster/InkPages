#include "inkpages/model/PageLayout.h"

#include <algorithm>
#include <cmath>

namespace inkpages
{
    PageLayout::PageLayout(std::vector<SizeF> pages, float gap, float margin)
    {
        float width = 0;
        for (SizeF size : pages)
        {
            width = std::max(width, size.width);
        }
        float y = margin;
        for (SizeF size : pages)
        {
            float x = margin + (width - size.width) / 2;
            rects.push_back({ x, y, x + size.width, y + size.height });
            y += size.height + gap;
        }
        document = { width + 2 * margin, pages.empty() ? 2 * margin : y - gap + margin };
    }

    std::optional<int> PageLayout::PageAt(PointF point) const
    {
        for (int i = 0; i < Count(); ++i)
        {
            if (rects[i].Contains(point))
            {
                return i;
            }
        }
        return std::nullopt;
    }

    std::optional<int> PageLayout::NearestPage(PointF point) const
    {
        std::optional<int> best;
        float bestDistance = 0;
        for (int i = 0; i < Count(); ++i)
        {
            RectF const& r = rects[i];
            float dy = point.y < r.top ? r.top - point.y : point.y > r.bottom ? point.y - r.bottom : 0;
            float dx = point.x < r.left ? r.left - point.x : point.x > r.right ? point.x - r.right : 0;
            float distance = std::hypot(dx, dy);
            if (!best || distance < bestDistance)
            {
                best = i;
                bestDistance = distance;
            }
        }
        return best;
    }

    std::optional<int> PageLayout::PageInView(RectF visible) const
    {
        std::optional<int> best;
        float bestCover = 0;
        for (int i = 0; i < Count(); ++i)
        {
            float cover = std::min(rects[i].bottom, visible.bottom) - std::max(rects[i].top, visible.top);
            if (cover > bestCover)
            {
                best = i;
                bestCover = cover;
            }
        }
        return best ? best : NearestPage({ (visible.left + visible.right) / 2, (visible.top + visible.bottom) / 2 });
    }

    std::vector<int> PageLayout::PagesIn(RectF area) const
    {
        std::vector<int> pages;
        for (int i = 0; i < Count(); ++i)
        {
            if (rects[i].Intersects(area))
            {
                pages.push_back(i);
            }
        }
        return pages;
    }
}
