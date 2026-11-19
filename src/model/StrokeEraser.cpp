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
        std::optional<std::pair<double, double>> Inside(PointF a, PointF b, PointF c, double r)
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

    std::optional<std::vector<Piece>> EraseCircle(std::vector<InkSample> const& samples,
        std::function<double(InkSample const&)> const& halfWidth, PointF center, double radius)
    {
        if (samples.empty()) return std::nullopt;
        if (samples.size() == 1) // a dot
        {
            return Distance(samples[0].position, center) < radius + halfWidth(samples[0])
                ? std::optional(std::vector<Piece>{}) : std::nullopt;
        }
        std::vector<Piece> pieces;
        Piece current;
        bool touched = false;
        if (Distance(samples[0].position, center) >= radius + halfWidth(samples[0])) current.push_back(samples[0]);
        for (size_t i = 1; i < samples.size(); ++i)
        {
            InkSample const& a = samples[i - 1];
            InkSample const& b = samples[i];
            double r = radius + std::max(halfWidth(a), halfWidth(b));
            if (current.empty() && Distance(a.position, center) >= r)
            {
                current.push_back(a); // outside for this segment (the previous one, wider, ended it inside)
            }
            auto inside = Inside(a.position, b.position, center, r);
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
}
