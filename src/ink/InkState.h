#pragma once

// The ink as the library keeps it (ink thread only): the presenter's stroke container, the page each stroke belongs to,
// and the page layout. A stroke never crosses a page: strokes are cut at the page's edge when they are added (Xournal++
// keeps strokes per page; pieces in the gap between pages are dropped).

#include <map>
#include <set>
#include <utility>
#include <vector>

#include "ink/Strokes.h"
#include "inkpages/model/PageLayout.h"
#include "inkpages/model/StrokeKeys.h"

namespace inkpages::detail
{
    using winrt::Windows::UI::Input::Inking::InkStrokeContainer;

    struct Placed
    {
        InkStroke stroke;
        int page;
    };

    struct InkState
    {
        InkStrokeContainer container{ nullptr };
        std::map<uint32_t, int> owner; // stroke id -> page
        StrokeKeys keys;               // the app's keys of loaded strokes, followed from object to object
        PageLayout layout;

        PointF Origin(int page) const { return { layout.Page(page).left, layout.Page(page).top }; }

        int PageOf(InkStroke const& stroke) const;
        void Add(InkStroke const& stroke, int page);
        /// Removes exactly these strokes from the container (by id).
        void Remove(std::vector<InkStroke> const& strokes);
        std::vector<InkStroke> StrokesOn(int page) const;
        std::vector<InkStroke> Find(std::set<uint32_t> const& ids) const;

        /// The stroke as it goes onto the pages: itself when it lies on one page, else its pieces cut at the pages'
        /// edges (each on its page; nothing of it in the gaps).
        std::vector<Placed> Split(InkStroke const& stroke) const;
    };
}
