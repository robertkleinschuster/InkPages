#pragma once

// Back and Forward between places of the document, as a PDF viewer has them: before a jump (a link, an outline entry,
// "Gehe zu Seite") the app records where the view was; Back returns there and Forward goes to the jump's target again.
// Places follow page operations (PageMap; places on a removed page are dropped). Pure (unit-tested).

#include <cstddef>
#include <optional>
#include <vector>

#include "inkpages/model/PageMap.h"
#include "inkpages/model/ViewMath.h"

namespace inkpages
{
    class ViewHistory
    {
    public:
        static constexpr std::size_t MaxPlaces = 100; // the oldest go first

        /// Before a jump: Back comes here. Forward places are dropped (a new way). The same place twice in a row is kept
        /// once.
        void Visit(ViewPlace from);
        /// The place to go back to (none: nothing recorded); `current` becomes the first Forward place.
        std::optional<ViewPlace> Back(ViewPlace current);
        /// The place to go forward to (none: nothing ahead); `current` becomes the first Back place.
        std::optional<ViewPlace> Forward(ViewPlace current);
        bool CanGoBack() const { return !back.empty(); }
        bool CanGoForward() const { return !forward.empty(); }
        void Clear();
        /// Pages inserted, removed or moved: the places follow; those on a removed page are dropped.
        void Remap(PageMap const& map);

    private:
        std::vector<ViewPlace> back, forward; // the last element is the nearest
    };
}
