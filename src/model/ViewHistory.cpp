#include "inkpages/model/ViewHistory.h"

#include <algorithm>
#include <cmath>

namespace inkpages
{
    namespace
    {
        // Within a page unit and the same zoom: the same place (a jump to where the view already is).
        bool Same(ViewPlace const& a, ViewPlace const& b)
        {
            return a.page == b.page && std::abs(a.point.x - b.point.x) < 1 && std::abs(a.point.y - b.point.y) < 1 &&
                std::abs(a.zoom - b.zoom) < 0.001f;
        }

        void Push(std::vector<ViewPlace>& places, ViewPlace place)
        {
            if (!places.empty() && Same(places.back(), place)) return;
            places.push_back(place);
            if (places.size() > ViewHistory::MaxPlaces) places.erase(places.begin());
        }

        void Follow(std::vector<ViewPlace>& places, PageMap const& map)
        {
            std::vector<ViewPlace> kept;
            for (ViewPlace place : places)
            {
                place.page = map(place.page);
                if (place.page >= 0) kept.push_back(place);
            }
            places = std::move(kept);
        }
    }

    void ViewHistory::Visit(ViewPlace from)
    {
        Push(back, from);
        forward.clear();
    }

    std::optional<ViewPlace> ViewHistory::Back(ViewPlace current)
    {
        if (back.empty()) return std::nullopt;
        ViewPlace to = back.back();
        back.pop_back();
        Push(forward, current);
        return to;
    }

    std::optional<ViewPlace> ViewHistory::Forward(ViewPlace current)
    {
        if (forward.empty()) return std::nullopt;
        ViewPlace to = forward.back();
        forward.pop_back();
        Push(back, current);
        return to;
    }

    void ViewHistory::Clear()
    {
        back.clear();
        forward.clear();
    }

    void ViewHistory::Remap(PageMap const& map)
    {
        Follow(back, map);
        Follow(forward, map);
    }
}
