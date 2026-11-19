#include "ink/InkState.h"

#include <winrt/Windows.Foundation.Collections.h>

namespace inkpages::detail
{
    int InkState::PageOf(InkStroke const& stroke) const
    {
        if (auto it = owner.find(stroke.Id()); it != owner.end()) return it->second;
        auto points = stroke.GetInkPoints();
        if (points.Size() == 0) return 0;
        auto first = points.GetAt(0).Position();
        return layout.NearestPage({ first.X, first.Y }).value_or(0);
    }

    void InkState::Add(InkStroke const& stroke, int page)
    {
        container.AddStroke(stroke);
        owner[stroke.Id()] = page;
    }

    void InkState::Remove(std::vector<InkStroke> const& strokes)
    {
        if (strokes.empty()) return;
        std::set<uint32_t> ids;
        for (InkStroke const& s : strokes) ids.insert(s.Id());
        // DeleteSelected is the container's only removal: exactly these strokes are marked, no other stays marked.
        for (InkStroke const& s : container.GetStrokes()) s.Selected(ids.contains(s.Id()));
        container.DeleteSelected();
        for (uint32_t id : ids) owner.erase(id);
    }

    std::vector<InkStroke> InkState::StrokesOn(int page) const
    {
        std::vector<InkStroke> strokes;
        for (InkStroke const& s : container.GetStrokes())
        {
            if (PageOf(s) == page) strokes.push_back(s);
        }
        return strokes;
    }

    std::vector<InkStroke> InkState::Find(std::set<uint32_t> const& ids) const
    {
        std::vector<InkStroke> strokes;
        for (InkStroke const& s : container.GetStrokes())
        {
            if (ids.contains(s.Id())) strokes.push_back(s);
        }
        return strokes;
    }

    std::vector<Placed> InkState::Split(InkStroke const& stroke) const
    {
        RectF bounds = BoundsOf(stroke);
        std::vector<int> pages = layout.PagesIn(bounds);
        auto samples = SamplesOf(stroke);
        if (pages.size() == 1)
        {
            RectF page = layout.Page(pages[0]);
            bool inside = true;
            for (InkSample const& s : samples) inside = inside && page.Contains(s.position);
            if (inside) return { { stroke, pages[0] } };
        }
        std::vector<Placed> placed;
        for (int page : pages)
        {
            for (Piece const& piece : ClipToRect(samples, layout.Page(page)))
            {
                placed.push_back({ MakeStroke(piece, stroke.DrawingAttributes()), page });
            }
        }
        return placed;
    }
}
