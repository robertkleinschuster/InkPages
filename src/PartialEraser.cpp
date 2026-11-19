#include "PartialEraser.h"

#include <algorithm>
#include <cmath>

#include <winrt/Windows.Foundation.Collections.h>

#include "inkpages/Pens.h"

namespace inkpages::detail
{
    void PartialEraser::Begin(PointF p)
    {
        edit = std::make_shared<StrokeEdit>(ink);
        last = p;
        sampled.clear();
        EraseAt({ p });
    }

    void PartialEraser::MoveAlong(std::vector<PointF> const& path)
    {
        if (!edit || path.empty()) return;
        std::vector<PointF> centers;
        for (PointF p : path)
        {
            float dx = p.x - last.x, dy = p.y - last.y;
            int steps = std::max(1, static_cast<int>(std::ceil(std::hypot(dx, dy) / (radius / 2))));
            for (int i = 1; i <= steps; ++i) centers.push_back({ last.x + dx * i / steps, last.y + dy * i / steps });
            last = p;
        }
        EraseAt(centers);
    }

    std::shared_ptr<StrokeEdit> PartialEraser::End()
    {
        auto done = std::move(edit);
        edit = nullptr;
        sampled.clear();
        return done;
    }

    void PartialEraser::EraseAt(std::vector<PointF> const& centers)
    {
        if (centers.empty()) return;
        float left = centers[0].x, right = left, top = centers[0].y, bottom = top;
        for (PointF c : centers)
        {
            left = std::min(left, c.x); right = std::max(right, c.x);
            top = std::min(top, c.y); bottom = std::max(bottom, c.y);
        }
        RectF sweep{ left - radius, top - radius, right + radius, bottom + radius };

        struct Cut
        {
            InkStroke stroke;
            std::vector<Piece> pieces;
            Sampled const* read;
        };
        std::vector<Cut> cuts;
        for (InkStroke const& stroke : ink.container.GetStrokes())
        {
            auto b = stroke.BoundingRect(); // includes the stroke's width
            if (!RectF{ b.X, b.Y, b.X + b.Width, b.Y + b.Height }.Intersects(sweep)) continue;
            uint32_t id = stroke.Id();
            auto it = sampled.find(id);
            if (it == sampled.end())
            {
                auto attributes = stroke.DrawingAttributes();
                it = sampled.emplace(id, Sampled{ SamplesOf(stroke), attributes, attributes.Size().Width,
                    attributes.IgnorePressure() }).first;
            }
            Sampled const& read = it->second;
            auto half = [&read](InkSample const& s) { return InkWidth::Drawn(read.size, read.ignorePressure, s.pressure) / 2.0; };
            std::vector<Piece> pieces{ read.samples };
            bool touched = false;
            for (PointF center : centers)
            {
                std::vector<Piece> next;
                for (Piece const& piece : pieces)
                {
                    auto cut = EraseCircle(piece, half, center, radius);
                    if (!cut) next.push_back(piece);
                    else
                    {
                        touched = true;
                        next.insert(next.end(), cut->begin(), cut->end());
                    }
                }
                pieces = std::move(next);
                if (pieces.empty()) break;
            }
            if (touched) cuts.push_back({ stroke, std::move(pieces), &read });
        }
        if (cuts.empty()) return;

        std::vector<InkStroke> gone;
        for (Cut& cut : cuts)
        {
            int page = ink.PageOf(cut.stroke);
            if (!edit->Forget(cut.stroke)) edit->Removed(cut.stroke, page); // it was there before this drag
            gone.push_back(cut.stroke);
            Sampled read = *cut.read;
            sampled.erase(cut.stroke.Id());
            for (Piece const& piece : cut.pieces)
            {
                InkStroke rest = MakeStroke(piece, read.attributes);
                edit->Added(rest, page);
                ink.Add(rest, page);
                sampled[rest.Id()] = Sampled{ piece, read.attributes, read.size, read.ignorePressure };
            }
        }
        ink.Remove(gone);
    }
}
