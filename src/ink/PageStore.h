#pragma once

// Lazy pages: memory is in the strokes (WinUI fact), so only pages near the view keep theirs in the presenter. A page
// going far hands its strokes to the app (`store`, page coordinates) and they leave the presenter; coming near it gets
// them back (`load`). Pages with history stay (an edit refers to their strokes). Ink thread only.

#include <functional>
#include <set>
#include <vector>

#include "ink/InkLayer.h"

namespace inkpages::detail
{
    class PageStore
    {
    public:
        explicit PageStore(InkLayer& ink) : ink(ink) {}

        std::function<std::vector<InkStroke>(int page)> load;
        std::function<void(int page, std::vector<InkStroke> strokes)> store;

        void SetLive(std::set<int> const& pages);
        /// Every live page goes to `store` (another document comes).
        void ReleaseAll();
        /// Copies of a live page's strokes in page coordinates (a page that is not live has none here: the app holds
        /// them since its `store`).
        std::vector<InkStroke> Snapshot(int page) const;
        bool IsLive(int page) const { return live.contains(page); }

    private:
        void Release(int page);
        void Restore(int page);

        InkLayer& ink;
        std::set<int> live;
    };
}
