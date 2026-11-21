#include "inkpages/model/StrokeEraser.h"

#include <algorithm>
#include <cmath>

namespace inkpages
{
    namespace
    {
        constexpr double MinPieceLength = 0.01; // shorter remains would only be a dot

        double Distance(PointF a, PointF b) { return std::hypot(double(a.x) - b.x, double(a.y) - b.y); }

        // The part [t1, t2] of segment a->b (t from 0 to 1) inside the circle, or nothing.
        using Interval = std::optional<std::pair<double, double>>;

        Interval InsideCircle(PointF a, PointF b, PointF c, double r)
        {
            double dx = double(b.x) - a.x, dy = double(b.y) - a.y;
            double fx = double(a.x) - c.x, fy = double(a.y) - c.y;
            double qa = dx * dx + dy * dy;
            double qc = fx * fx + fy * fy - r * r;
            if (qa < 1e-12)
            {
                return qc < 0 ? std::optional(std::pair(0.0, 1.0)) : std::nullopt; // a zero-length segment
            }
            double qb = 2 * (dx * fx + dy * fy);
            double disc = qb * qb - 4 * qa * qc;
            if (disc <= 0) return std::nullopt; // passes by (or only touches)
            double root = std::sqrt(disc);
            double t1 = (-qb - root) / (2 * qa), t2 = (-qb + root) / (2 * qa);
            if (t2 <= 0 || t1 >= 1) return std::nullopt;
            return std::pair(std::max(0.0, t1), std::min(1.0, t2));
        }

        Interval InsideBox(PointF a, PointF b, PointF c, double width, double height)
        {
            double first = 0, last = 1;
            auto clip = [&](double from, double to, double center, double extent)
            {
                double delta = to - from;
                if (delta == 0) return std::abs(from - center) <= extent;
                double t1 = (center - extent - from) / delta, t2 = (center + extent - from) / delta;
                if (t1 > t2) std::swap(t1, t2);
                first = std::max(first, t1);
                last = std::min(last, t2);
                return first < last;
            };
            if (!clip(a.x, b.x, c.x, width) || !clip(a.y, b.y, c.y, height)) return std::nullopt;
            return std::pair(first, last);
        }

        // A round stroke touches the square when its centerline enters the square expanded by its half-width.
        // The expansion is convex and consists of two rectangles and four rounded corners.
        Interval InsideSquare(PointF a, PointF b, PointF c, double side, double padding)
        {
            Interval span = InsideBox(a, b, c, side + padding, side);
            auto merge = [&](Interval part)
            {
                if (!part) return;
                if (!span) span = part;
                else
                {
                    span->first = std::min(span->first, part->first);
                    span->second = std::max(span->second, part->second);
                }
            };
            merge(InsideBox(a, b, c, side, side + padding));
            float offset = static_cast<float>(side);
            for (float x : { c.x - offset, c.x + offset })
                for (float y : { c.y - offset, c.y + offset }) merge(InsideCircle(a, b, { x, y }, padding));
            return span;
        }

        std::vector<Interval> SquareCenterlineCuts(Piece const& samples, PointF center, double side, double padding)
        {
            std::vector<Interval> cuts(samples.size());
            size_t runStart = 1;
            bool entersInner = false;
            for (size_t i = 1; i < samples.size(); ++i)
            {
                PointF a = samples[i - 1].position, b = samples[i].position;
                double outer = side + padding;
                cuts[i] = InsideBox(a, b, center, outer, outer);
                if (cuts[i])
                {
                    entersInner |= InsideBox(a, b, center, side, side).has_value();
                    if (cuts[i]->second == 1 && i + 1 < samples.size()) continue;
                }
                if (!entersInner) std::fill(cuts.begin() + runStart, cuts.begin() + i + 1, std::nullopt);
                runStart = i + 1;
                entersInner = false;
            }
            return cuts;
        }

        InkSample At(InkSample const& a, InkSample const& b, double t)
        {
            return { { static_cast<float>(a.position.x + t * (b.position.x - a.position.x)),
                         static_cast<float>(a.position.y + t * (b.position.y - a.position.y)) },
                static_cast<float>(a.pressure + t * (b.pressure - a.pressure)) };
        }

        void Close(Piece& piece, std::vector<Piece>& pieces)
        {
            double length = 0;
            for (size_t i = 1; i < piece.size(); ++i) length += Distance(piece[i - 1].position, piece[i].position);
            if (piece.size() >= 2 && length >= MinPieceLength) pieces.push_back(std::move(piece));
            piece.clear();
        }
    }

    static std::optional<std::vector<Piece>> Erase(std::vector<InkSample> const& samples,
        std::function<double(InkSample const&)> const& halfWidth, PointF center, double extent, bool square,
        std::optional<double> squarePadding = std::nullopt)
    {
        if (samples.empty()) return std::nullopt;
        auto insideAt = [&](PointF p, double padding)
        {
            if (!square) return Distance(p, center) < extent + padding;
            double dx = std::max(0.0, std::abs(double(p.x) - center.x) - extent);
            double dy = std::max(0.0, std::abs(double(p.y) - center.y) - extent);
            if (squarePadding) return dx == 0 && dy == 0;
            return std::hypot(dx, dy) <= padding;
        };
        if (samples.size() == 1) // a dot
        {
            return insideAt(samples[0].position, halfWidth(samples[0]))
                ? std::optional(std::vector<Piece>{}) : std::nullopt;
        }
        std::vector<Piece> pieces;
        Piece current;
        bool touched = false;
        if (!insideAt(samples[0].position, halfWidth(samples[0]))) current.push_back(samples[0]);
        auto cuts = squarePadding ? SquareCenterlineCuts(samples, center, extent, *squarePadding) : std::vector<Interval>{};
        for (size_t i = 1; i < samples.size(); ++i)
        {
            InkSample const& a = samples[i - 1];
            InkSample const& b = samples[i];
            double padding = std::max(halfWidth(a), halfWidth(b));
            auto inside = squarePadding ? cuts[i] : square ? InsideSquare(a.position, b.position, center, extent, padding)
                                 : InsideCircle(a.position, b.position, center, extent + padding);
            if (current.empty() && (!inside || inside->first > 0)) current.push_back(a);
            if (!inside)
            {
                current.push_back(b);
                continue;
            }
            touched = true;
            auto [t1, t2] = *inside;
            if (t1 > 0) current.push_back(At(a, b, t1));
            Close(current, pieces);
            if (t2 < 1)
            {
                current.push_back(At(a, b, t2));
                current.push_back(b);
            }
        }
        if (!touched) return std::nullopt;
        Close(current, pieces);
        return pieces;
    }

    std::optional<std::vector<Piece>> EraseCircle(std::vector<InkSample> const& samples,
        std::function<double(InkSample const&)> const& halfWidth, PointF center, double radius)
    {
        return Erase(samples, halfWidth, center, radius, false);
    }

    std::optional<std::vector<Piece>> EraseSquare(std::vector<InkSample> const& samples,
        std::function<double(InkSample const&)> const& halfWidth, PointF center, double halfSide)
    {
        return Erase(samples, halfWidth, center, halfSide, true);
    }

    std::optional<std::vector<Piece>> EraseSquareCenterline(Piece const& samples, PointF center, double halfSide, double padding)
    {
        return Erase(samples, [](InkSample const&) { return 0.; }, center, halfSide, true, padding);
    }
}
