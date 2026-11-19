#pragma once

// The view's geometry: DirectManipulation's content transform (zoom and translation in pixels) and the DPI scale
// between document DIPs and the viewport's pixels. Pure math (unit-tested).
//   viewport pixel = document DIP * scale * zoom + offset

#include <set>

#include "inkpages/PageLayout.h"

namespace inkpages
{
    struct View
    {
        float zoom = 1;
        float dx = 0, dy = 0; // pixels
        friend bool operator==(View, View) = default;
    };

    float ClampZoom(float zoom, float minZoom, float maxZoom);

    /// The zoom at which the document's width fills the viewport's width.
    float FitWidthZoom(float documentWidth, float viewportWidthPx, float scale);

    PointF DocumentToViewport(PointF document, View view, float scale);
    PointF ViewportToDocument(PointF viewport, View view, float scale);

    /// The document rectangle the viewport shows.
    RectF VisibleDocument(SizeF viewportPx, View view, float scale);

    /// The content rectangle (DirectManipulation's content pixels = document DIPs * scale) to zoom to so that the
    /// zoom becomes `zoom` and the document point under `anchorPx` stays where it is.
    RectF ZoomRect(SizeF viewportPx, View view, float zoom, PointF anchorPx);

    /// The content rectangle that shows `documentTop` at the viewport's top at the current zoom, left as it is.
    RectF ScrollRect(SizeF viewportPx, View view, float scale, float documentTop);

    /// Lazy pages: which pages keep (or get) their ink. Pages within one viewport height of the visible area come in;
    /// pages already live stay until they are more than two viewport heights away; held pages always stay.
    std::set<int> LivePages(PageLayout const& layout, RectF visible, std::set<int> const& live, std::set<int> const& held);
}
