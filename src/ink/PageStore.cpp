#include "ink/PageStore.h"

#include <format>

#include "inkpages/Log.h"

namespace inkpages::detail
{
    void PageStore::SetLive(std::set<int> const& pages)
    {
        for (int page : std::set<int>(live))
        {
            if (!pages.contains(page) && !ink.Edits().Holds(page)) Release(page);
        }
        for (int page : pages)
        {
            if (!live.contains(page)) Restore(page);
        }
    }

    void PageStore::ReleaseAll()
    {
        for (int page : std::set<int>(live)) Release(page);
    }

    std::vector<InkStroke> PageStore::Snapshot(int page) const
    {
        auto& state = ink.State();
        if (page < 0 || page >= state.layout.Count()) return {};
        RectF r = state.layout.Page(page);
        std::vector<InkStroke> strokes;
        for (InkStroke const& s : state.StrokesOn(page)) strokes.push_back(Transformed(s, Affine::Translation(-r.left, -r.top)));
        return strokes;
    }

    void PageStore::Release(int page)
    {
        auto& state = ink.State();
        auto strokes = Snapshot(page);
        state.Remove(state.StrokesOn(page));
        live.erase(page);
        Log(std::format("ink: page {} released ({} strokes)", page, strokes.size()));
        if (store) store(page, std::move(strokes));
    }

    void PageStore::Restore(int page)
    {
        live.insert(page);
        if (!load) return;
        auto& state = ink.State();
        RectF r = state.layout.Page(page);
        std::vector<InkStroke> strokes = load(page);
        for (InkStroke const& s : strokes) state.Add(Transformed(s, Affine::Translation(r.left, r.top)), page);
        if (!strokes.empty()) Log(std::format("ink: page {} restored ({} strokes)", page, strokes.size()));
    }
}
