#pragma once

// The view's geometry: DirectManipulation's content transform (zoom and translation in pixels) and the DPI scale
// between document DIPs and the viewport's pixels. Pure math (unit-tested).
//   viewport pixel = document DIP * scale * zoom + offset

#include <optional>
#include <set>

#include "inkpages/model/PageLayout.h"

namespace inkpages
{
    struct View
    {
        float zoom = 1;
        float dx = 0, dy = 0; // pixels
        friend bool operator==(View, View) = default;
    };

    /// A place in the document as the view shows it: the page and the point of it (page units, may lie beside the page)
    /// at the view's top left, and the zoom. Page-relative, so it stays right when the layout changes (Back/Forward, the
    /// last position of a document).
    struct ViewPlace
    {
        int page = 0;
        PointF point;
        float zoom = 1;
    };

    /// What a scroll key does (the surface maps the keyboard to it).
    enum class ScrollKey { LineUp, LineDown, LineLeft, LineRight, PageUp, PageDown, Start, End };

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

    /// A rectangle to zoom to, kept on the content (content pixels, `content` = its size): a side longer than the
    /// content is centered on it (horizontally) or starts at its top (vertically); a shorter one is moved inside it.
    /// DirectManipulation's ZoomToRect does not do that itself (measured: zooming out at the top left the content
    /// 144 px below the view's top).
    RectF KeepOnContent(RectF rect, SizeF content);

    /// The content rectangle that shows `documentTop` at the viewport's top at the current zoom, left as it is.
    RectF ScrollRect(SizeF viewportPx, View view, float scale, float documentTop);

    /// The content rectangle that brings document point `at` to the viewport's top left, `marginPx` (viewport pixels)
    /// away from the edges, at the current zoom.
    RectF ScrollToPointRect(SizeF viewportPx, View view, float scale, PointF at, float marginPx);

    /// The content rectangle that brings `area` (document) into view with `marginPx` around it, moving as little as
    /// needed at the current zoom; none when it is in view already. An area larger than the view shows its top left.
    std::optional<RectF> RevealRect(SizeF viewportPx, View view, float scale, RectF area, float marginPx);

    /// The content rectangle moved by (dxPx, dyPx) viewport pixels at the current zoom (positive: the view moves right/down
    /// over the document, the content up/left) - panning.
    RectF PanRect(SizeF viewportPx, View view, float dxPx, float dyPx);

    SizeF ViewportWithInputPane(SizeF client, RectF pane);
    PointF CaretScroll(SizeF viewport, RectF caret, RectF pane, float margin);
    // Keeps the caret out of the pane after clamping to the document. Zooms only if panning cannot clear an overlap.
    std::optional<RectF> RevealCaretRect(SizeF viewport, View view, SizeF content, RectF caret, RectF pane,
        float margin, float maxZoom);

    /// The content rectangle for a scroll key at the current zoom: a line = `linePx` viewport pixels, a page = the view's
    /// height less `overlapPx` (what stays in view), Start/End = the document's top/bottom (`documentHeight` in DIPs).
    RectF KeyScrollRect(ScrollKey key, SizeF viewportPx, View view, float scale, float documentHeight, float linePx,
        float overlapPx);

    /// The content rectangle that shows `page` (document) whole with `marginDip` around it, centered, zoom clamped.
    RectF FitPageRect(RectF page, SizeF viewportPx, float scale, float marginDip, float minZoom, float maxZoom);

    /// The place the view shows: `visible` = VisibleDocument; the page is the page in view (PageLayout::PageInView: a
    /// view whose top lies in a gap names the page below, not the one above). None without pages.
    std::optional<ViewPlace> PlaceOf(PageLayout const& layout, RectF visible, float zoom);

    /// The content rectangle that shows `place` (its point at the view's top left at its zoom, clamped); the page index is
    /// clamped to the document. Call KeepOnContent on it.
    RectF PlaceRect(PageLayout const& layout, ViewPlace place, SizeF viewportPx, float scale, float minZoom, float maxZoom);

    /// Lazy pages: which pages keep (or get) their ink. Pages within one viewport height of the visible area come in;
    /// pages already live stay until they are more than two viewport heights away; held pages always stay.
    std::set<int> LivePages(PageLayout const& layout, RectF visible, std::set<int> const& live, std::set<int> const& held);
}
