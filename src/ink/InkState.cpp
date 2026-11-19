#include "ink/InkState.h"

#include <winrt/Windows.Foundation.Collections.h>

namespace inkpages::detail
{
    InkStroke InkState::Current(InkStroke const& stroke) const
    {
        InkStroke now = stroke;
        for (auto it = replacedBy.find(now.Id()); it != replacedBy.end(); it = replacedBy.find(now.Id())) now = it->second;
        return now;
    }

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

    void InkState::BeginRelocation(PageMap const& map, PageLayout next)
    {
        Relocation r;
        for (int page = 0; page < layout.Count(); ++page)
        {
            int to = map(page);
            r.shift.push_back(to < 0 ? PointF{} : PointF{ next.Page(to).left - layout.Page(page).left, next.Page(to).top - layout.Page(page).top });
        }
        relocation = std::move(r);
        std::vector<InkStroke> leaving; // strokes of removed pages, and the originals of moved strokes
        std::vector<Placed> moved;
        std::map<uint32_t, int> owners;
        for (InkStroke const& s : container.GetStrokes())
        {
            int page = PageOf(s);
            int to = map(page);
            if (to < 0)
            {
                keys.Forget(s.Id());
                leaving.push_back(s);
                continue;
            }
            InkStroke at = Relocated(s, page);
            if (at != s)
            {
                leaving.push_back(s);
                moved.push_back({ at, to });
            }
            else owners[s.Id()] = to;
        }
        Remove(leaving);
        owner = std::move(owners);
        for (Placed const& p : moved) Add(p.stroke, p.page);
        layout = std::move(next);
    }

    InkStroke InkState::Relocated(InkStroke const& stroke, int oldPage)
    {
        if (!relocation || oldPage < 0 || oldPage >= static_cast<int>(relocation->shift.size())) return stroke;
        if (auto it = relocation->copies.find(stroke.Id()); it != relocation->copies.end()) return it->second;
        if (relocation->made.contains(stroke.Id())) return stroke;
        PointF d = relocation->shift[oldPage];
        if (d.x == 0 && d.y == 0) return stroke;
        Affine m = Affine::Translation(d.x, d.y);
        InkStroke copy = Transformed(stroke, m);
        keys.Moved(stroke.Id(), copy.Id(), m); // the app's key follows (its page coordinates stay the same)
        relocation->copies.emplace(stroke.Id(), copy);
        relocation->made.insert(copy.Id());
        Replaced(stroke, copy);
        return copy;
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
