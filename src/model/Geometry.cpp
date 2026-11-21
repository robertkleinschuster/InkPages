#include "inkpages/model/Geometry.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace inkpages
{
    RectF Affine::Bounds(RectF r) const
    {
        PointF corners[] = { Apply(PointF{ r.left, r.top }), Apply(PointF{ r.right, r.top }), Apply(PointF{ r.right, r.bottom }), Apply(PointF{ r.left, r.bottom }) };
        RectF out{ corners[0].x, corners[0].y, corners[0].x, corners[0].y };
        for (PointF c : corners)
        {
            out.left = std::min(out.left, c.x); out.right = std::max(out.right, c.x);
            out.top = std::min(out.top, c.y); out.bottom = std::max(out.bottom, c.y);
        }
        return out;
    }

    Affine Affine::Then(Affine const& n) const
    {
        return { m11 * n.m11 + m12 * n.m21, m11 * n.m12 + m12 * n.m22,
            m21 * n.m11 + m22 * n.m21, m21 * n.m12 + m22 * n.m22,
            dx * n.m11 + dy * n.m21 + n.dx, dx * n.m12 + dy * n.m22 + n.dy };
    }

    std::optional<Affine> Affine::Inverse() const
    {
        float det = m11 * m22 - m12 * m21;
        if (std::abs(det) < 1e-12f) return std::nullopt;
        float i11 = m22 / det, i12 = -m12 / det, i21 = -m21 / det, i22 = m11 / det;
        return Affine{ i11, i12, i21, i22, -(dx * i11 + dy * i21), -(dx * i12 + dy * i22) };
    }

    RectF SelectionFrame::HandleRect(Handle h) const
    {
        RectF f = Frame();
        float s = handle / 2;
        PointF c;
        switch (h)
        {
        case Handle::TopLeft: c = { f.left, f.top }; break;
        case Handle::TopRight: c = { f.right, f.top }; break;
        case Handle::BottomRight: c = { f.right, f.bottom }; break;
        case Handle::BottomLeft: c = { f.left, f.bottom }; break;
        case Handle::Rotate: c = { f.right + 2 * handle, (f.top + f.bottom) / 2 }; s = handle; break;
        case Handle::Delete: c = { f.left - 2 * handle, f.top }; s = handle; break;
        case Handle::Duplicate: c = { f.left - 2 * handle, f.top + 2.5f * handle }; s = handle; break;
        default: return {};
        }
        return { c.x - s, c.y - s, c.x + s, c.y + s };
    }

    Handle SelectionFrame::At(PointF p) const
    {
        for (Handle h : { Handle::TopLeft, Handle::TopRight, Handle::BottomRight, Handle::BottomLeft, Handle::Rotate, Handle::Delete, Handle::Duplicate })
        {
            RectF r = HandleRect(h).Inflated(handle / 4, handle / 4); // a little more than drawn: easy to hit with a pen
            if (p.x >= r.left && p.x <= r.right && p.y >= r.top && p.y <= r.bottom) return h;
        }
        RectF f = Frame();
        return p.x >= f.left && p.x <= f.right && p.y >= f.top && p.y <= f.bottom ? Handle::Move : Handle::None;
    }

    Affine SelectionFrame::Drag(Handle grabbed, PointF from, PointF to) const
    {
        if (grabbed == Handle::Move) return Affine::Translation(to.x - from.x, to.y - from.y);
        if (grabbed == Handle::None || IsSelectionCommand(grabbed)) return {};
        if (grabbed == Handle::Rotate)
        {
            PointF pivot{ (bounds.left + bounds.right) / 2, (bounds.top + bounds.bottom) / 2 };
            float angle = std::atan2(to.y - pivot.y, to.x - pivot.x) - std::atan2(from.y - pivot.y, from.x - pivot.x);
            float c = std::cos(angle), s = std::sin(angle);
            return Affine::Translation(-pivot.x, -pivot.y).Then({ c, s, -s, c, 0, 0 })
                .Then(Affine::Translation(pivot.x, pivot.y));
        }
        RectF f = Frame();
        // The opposite corner stays; the grabbed one follows the pointer, keeping the proportions.
        PointF anchor, corner;
        switch (grabbed)
        {
        case Handle::TopLeft: anchor = { f.right, f.bottom }; corner = { f.left, f.top }; break;
        case Handle::TopRight: anchor = { f.left, f.bottom }; corner = { f.right, f.top }; break;
        case Handle::BottomRight: anchor = { f.left, f.top }; corner = { f.right, f.bottom }; break;
        default: anchor = { f.right, f.top }; corner = { f.left, f.bottom }; break;
        }
        PointF moved{ corner.x + (to.x - from.x), corner.y + (to.y - from.y) };
        float w = corner.x - anchor.x, h = corner.y - anchor.y;
        float sx = w != 0 ? (moved.x - anchor.x) / w : 1, sy = h != 0 ? (moved.y - anchor.y) / h : 1;
        float s = std::abs(sx - 1) > std::abs(sy - 1) ? sx : sy;
        float minimum = 4 / std::max(std::abs(w), std::abs(h)); // never smaller than a few units (or mirrored)
        s = std::max(s, minimum);
        return Affine::ScaleAround(s, anchor.x, anchor.y);
    }

    std::vector<Piece> ClipToRect(std::vector<InkSample> const& samples, RectF r)
    {
        std::vector<Piece> pieces;
        Piece current;
        auto inside = [&](PointF p) { return p.x >= r.left && p.x <= r.right && p.y >= r.top && p.y <= r.bottom; };
        auto at = [](InkSample const& a, InkSample const& b, float t) {
            return InkSample{ { a.position.x + t * (b.position.x - a.position.x), a.position.y + t * (b.position.y - a.position.y) },
                a.pressure + t * (b.pressure - a.pressure) };
        };
        // Liang-Barsky: the part [t0, t1] of a segment inside the rectangle.
        auto clip = [&](PointF a, PointF b, float& t0, float& t1) {
            float dx = b.x - a.x, dy = b.y - a.y;
            float p[] = { -dx, dx, -dy, dy }, q[] = { a.x - r.left, r.right - a.x, a.y - r.top, r.bottom - a.y };
            t0 = 0; t1 = 1;
            for (int i = 0; i < 4; ++i)
            {
                if (p[i] == 0) { if (q[i] < 0) return false; continue; }
                float t = q[i] / p[i];
                if (p[i] < 0) t0 = std::max(t0, t); else t1 = std::min(t1, t);
                if (t0 > t1) return false;
            }
            return true;
        };
        if (samples.size() == 1)
        {
            if (inside(samples[0].position)) pieces.push_back(samples);
            return pieces;
        }
        for (size_t i = 1; i < samples.size(); ++i)
        {
            InkSample const& a = samples[i - 1];
            InkSample const& b = samples[i];
            float t0, t1;
            if (!clip(a.position, b.position, t0, t1))
            {
                if (current.size() >= 2) pieces.push_back(current);
                current.clear();
                continue;
            }
            if (current.empty()) current.push_back(t0 > 0 ? at(a, b, t0) : a);
            current.push_back(t1 < 1 ? at(a, b, t1) : b);
            if (t1 < 1)
            {
                if (current.size() >= 2) pieces.push_back(current);
                current.clear();
            }
        }
        if (current.size() >= 2) pieces.push_back(current);
        return pieces;
    }

    bool InPolygon(std::vector<PointF> const& polygon, PointF p)
    {
        bool in = false;
        for (size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++)
        {
            PointF a = polygon[i], b = polygon[j];
            if ((a.y > p.y) != (b.y > p.y) && p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x) in = !in;
        }
        return in;
    }

    namespace
    {
        constexpr int EllipseSteps = 72;

        std::vector<PointF> Ellipse(PointF c, float rx, float ry, float angle)
        {
            std::vector<PointF> points;
            float cs = std::cos(angle), sn = std::sin(angle);
            for (int i = 0; i <= EllipseSteps; ++i)
            {
                float t = 2 * std::numbers::pi_v<float> * i / EllipseSteps;
                float x = rx * std::cos(t), y = ry * std::sin(t);
                points.push_back({ c.x + x * cs - y * sn, c.y + x * sn + y * cs });
            }
            points.back() = points.front(); // closed exactly
            return points;
        }

        float Distance(PointF a, PointF b) { return std::hypot(a.x - b.x, a.y - b.y); }
    }

    std::vector<PointF> ShapeOutline(ShapeKind kind, PointF start, PointF end, float penWidth)
    {
        switch (kind)
        {
        case ShapeKind::Line: return { start, end };
        case ShapeKind::Rectangle: return { start, { end.x, start.y }, end, { start.x, end.y }, start };
        case ShapeKind::Ellipse:
            return Ellipse({ (start.x + end.x) / 2, (start.y + end.y) / 2 }, std::abs(end.x - start.x) / 2, std::abs(end.y - start.y) / 2, 0);
        case ShapeKind::Arrow:
        {
            float length = Distance(start, end);
            if (length == 0) return { start, end };
            float head = std::min(std::max(8.f, 4 * penWidth), length * 0.4f);
            float angle = std::atan2(end.y - start.y, end.x - start.x);
            constexpr float Spread = 25 * std::numbers::pi_v<float> / 180;
            auto side = [&](float a) { return PointF{ end.x - head * std::cos(a), end.y - head * std::sin(a) }; };
            return { start, end, side(angle - Spread), end, side(angle + Spread) };
        }
        }
        return {};
    }

    std::vector<PointF> EllipseFromAxes(PointF center, std::vector<PointF> const& axes)
    {
        if (axes.size() < 4) return {};
        float rx = Distance(axes[0], axes[2]) / 2, ry = Distance(axes[1], axes[3]) / 2;
        float angle = std::atan2(axes[2].y - axes[0].y, axes[2].x - axes[0].x);
        return Ellipse(center, rx, ry, angle);
    }
}
