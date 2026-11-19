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

    std::vector<KeyedStroke> PageStore::Snapshot(int page) const
    {
        auto& state = ink.State();
        if (page < 0 || page >= state.layout.Count()) return {};
        PointF origin = state.Origin(page);
        std::vector<KeyedStroke> strokes;
        for (InkStroke const& s : state.StrokesOn(page))
        {
            KeyedStroke keyed{ Transformed(s, Affine::Translation(-origin.x, -origin.y)) };
            if (auto key = state.keys.Find(s.Id(), origin))
            {
                keyed.key = key->key;
                keyed.transform = key->transform;
            }
            strokes.push_back(std::move(keyed));
        }
        return strokes;
    }

    void PageStore::Release(int page)
    {
        auto& state = ink.State();
        auto strokes = Snapshot(page);
        auto leaving = state.StrokesOn(page);
        state.Remove(leaving);
        for (InkStroke const& s : leaving) state.keys.Forget(s.Id());
        live.erase(page);
        Log(std::format("ink: page {} released ({} strokes)", page, strokes.size()));
        if (store) store(page, std::move(strokes));
    }

    void PageStore::Restore(int page)
    {
        live.insert(page);
        if (!load) return;
        auto& state = ink.State();
        PointF origin = state.Origin(page);
        std::vector<KeyedStroke> strokes = load(page);
        for (KeyedStroke const& s : strokes)
        {
            InkStroke placed = Transformed(s.stroke, Affine::Translation(origin.x, origin.y));
            state.keys.Loaded(placed.Id(), s.key, s.transform, origin);
            state.Add(placed, page);
        }
        if (!strokes.empty()) Log(std::format("ink: page {} restored ({} strokes)", page, strokes.size()));
    }
}
