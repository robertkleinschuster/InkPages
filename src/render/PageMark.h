#pragma once

// The outline around the page in view (as the WinUI version's MarkCurrentPage: 2 page units wide, 3 away from the page,
// corner radius 4, in the accent color) - below the sheets, so it is never over the ink and never part of a page's
// content. Four thin strips (top and bottom with the rounded corners, left and right) instead of one page-sized surface:
// drawn at the settled zoom's resolution, they stay crisp and small at any zoom.

#include <dcomp.h>

#include <array>
#include <optional>

#include <winrt/base.h>

#include "inkpages/model/PageLayout.h"
#include "inkpages/model/Paper.h"

namespace inkpages::detail
{
    struct Graphics;

    class PageMark
    {
    public:
        static constexpr float Gap = 3, Thickness = 2, Radius = 4; // page units (WinUI: MarkGap, MarkThickness, CornerRadius)

        PageMark(Graphics& graphics, IDCompositionVisual* parent);
        PageMark(PageMark const&) = delete;
        PageMark& operator=(PageMark const&) = delete;

        /// Outlines `page` (document) at `renderScale` pixels per page unit in `color`; none: no outline.
        void Show(std::optional<RectF> page, Color color, float renderScale);

    private:
        void DrawStrip(int index, RectF strip, RectF outline, Color color, float scale);

        Graphics& graphics;
        winrt::com_ptr<IDCompositionVisual> visual;
        std::array<winrt::com_ptr<IDCompositionVisual>, 4> strips;
        std::optional<RectF> shown;
        Color shownColor;
        float shownScale = 0;
    };
}
