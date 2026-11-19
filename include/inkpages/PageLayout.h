#pragma once

// Where the pages lie in the document: a vertical stack, centered horizontally, with a gap between pages and a margin
// around them. Document units are DIPs. Pure geometry (unit-tested).

#include <optional>
#include <vector>

#include "inkpages/Paper.h"

namespace inkpages
{
    struct SizeF
    {
        float width = 0, height = 0;
        friend bool operator==(SizeF, SizeF) = default;
    };

    struct RectF
    {
        float left = 0, top = 0, right = 0, bottom = 0;
        float Width() const { return right - left; }
        float Height() const { return bottom - top; }
        bool Contains(PointF p) const { return p.x >= left && p.x < right && p.y >= top && p.y < bottom; }
        bool Intersects(RectF const& o) const { return left < o.right && o.left < right && top < o.bottom && o.top < bottom; }
        RectF Inflated(float dx, float dy) const { return { left - dx, top - dy, right + dx, bottom + dy }; }
        friend bool operator==(RectF const&, RectF const&) = default;
    };

    class PageLayout
    {
    public:
        PageLayout() = default;
        PageLayout(std::vector<SizeF> pages, float gap = 24, float margin = 24);

        int Count() const { return static_cast<int>(rects.size()); }
        SizeF DocumentSize() const { return document; }
        RectF Page(int index) const { return rects.at(index); }

        /// The page under a document point (gaps and margins: none).
        std::optional<int> PageAt(PointF point) const;

        /// The page nearest to a document point (a point in a gap belongs to the closer page); none without pages.
        std::optional<int> NearestPage(PointF point) const;

        /// The page in view: the one that covers most of the visible rectangle's height (ties: the upper one).
        std::optional<int> PageInView(RectF visible) const;

        /// Pages that intersect the rectangle, in order.
        std::vector<int> PagesIn(RectF area) const;

    private:
        std::vector<RectF> rects;
        SizeF document;
    };
}
