#pragma once

// Geometry of the tools (pure, unit-tested): affine transforms, the selection's frame and its corner handles, taps,
// clipping strokes at a page's edge, and the shapes' outlines. Ported from Model/Tap.cs and Model/ShapeGeometry.cs
// (winui-final); the clipping is new (one presenter over the document: strokes are cut at the page edge, as Xournal++
// keeps strokes per page).

#include <optional>
#include <vector>

#include "inkpages/model/PageLayout.h"
#include "inkpages/model/StrokeEraser.h"

namespace inkpages
{
    /// x' = m11 x + m21 y + dx, y' = m12 x + m22 y + dy (the layout of D2D/WinRT 3x2 matrices).
    struct Affine
    {
        float m11 = 1, m12 = 0, m21 = 0, m22 = 1, dx = 0, dy = 0;

        static Affine Translation(float x, float y) { return { 1, 0, 0, 1, x, y }; }
        /// Scales by s around the point (cx, cy).
        static Affine ScaleAround(float s, float cx, float cy) { return { s, 0, 0, s, cx - s * cx, cy - s * cy }; }
        PointF Apply(PointF p) const { return { m11 * p.x + m21 * p.y + dx, m12 * p.x + m22 * p.y + dy }; }
        RectF Bounds(RectF r) const;         // the bounds of the transformed rectangle
        Affine Then(Affine const& next) const; // this, then next
        std::optional<Affine> Inverse() const;
        bool IsIdentity() const { return m11 == 1 && m12 == 0 && m21 == 0 && m22 == 1 && dx == 0 && dy == 0; }
        friend bool operator==(Affine const&, Affine const&) = default;
    };

    /// A press on the selection: a tap only when short and (nearly) still - any other drag moves exactly, however small
    /// (improving a sketch needs moves of a fraction of a line width).
    struct Tap
    {
        static constexpr float MaxDistance = 3;     // page units, |dx| + |dy|
        static constexpr unsigned MaxDurationMs = 300;
        static bool IsTap(float farthest, unsigned durationMs) { return farthest < MaxDistance && durationMs < MaxDurationMs; }
    };

    enum class Handle { None, Move, TopLeft, TopRight, BottomRight, BottomLeft };

    /// The selection's frame: its bounds with a margin, and the corner handles (scaling) - sizes in page units.
    struct SelectionFrame
    {
        RectF bounds;            // what is selected
        float margin = 4;        // the frame around it
        float handle = 8;        // a corner handle's side

        RectF Frame() const { return bounds.Inflated(margin, margin); }
        RectF HandleRect(Handle h) const;
        /// What a point grabs: a corner handle, the inside (move), or nothing.
        Handle At(PointF p) const;
        /// The transform of dragging `grabbed` from `from` to `to`: a move, or a proportional scale around the opposite
        /// corner (the corner follows the pointer's larger relative change; never smaller than a few units).
        Affine Drag(Handle grabbed, PointF from, PointF to) const;
    };

    /// The pieces of a polyline inside a rectangle (cut where it crosses the edge; the pressure interpolated there).
    std::vector<Piece> ClipToRect(std::vector<InkSample> const& samples, RectF rect);

    /// Whether a point is inside a polygon (even-odd rule).
    bool InPolygon(std::vector<PointF> const& polygon, PointF p);

    enum class ShapeKind { Line, Arrow, Rectangle, Ellipse };

    /// The outline of a shape dragged from `start` to `end` (an arrow's head grows with the pen's width).
    std::vector<PointF> ShapeOutline(ShapeKind kind, PointF start, PointF end, float penWidth);
    /// An ellipse from the ends of both axes (0-2 and 1-3), as Windows ink analysis reports circles and ellipses.
    std::vector<PointF> EllipseFromAxes(PointF center, std::vector<PointF> const& axes);
}
