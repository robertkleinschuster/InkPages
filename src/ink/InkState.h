#pragma once

// The ink as the library keeps it (ink thread only): the presenter's stroke container, the page each stroke belongs to,
// and the page layout. A stroke never crosses a page: strokes are cut at the page's edge when they are added (Xournal++
// keeps strokes per page; pieces in the gap between pages are dropped).

#include <map>
#include <optional>
#include <set>
#include <utility>
#include <vector>

#include "ink/Strokes.h"
#include "inkpages/model/PageLayout.h"
#include "inkpages/model/PageMap.h"
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

        /// Pages were inserted, removed or moved (`next` is the new layout): strokes of removed pages leave, strokes of
        /// moved pages are rebuilt at their page's new place (their keys follow), the page owners follow the map. Strokes
        /// that edits still refer to are moved by `Relocated` during the same pass (EndRelocation ends it).
        void BeginRelocation(PageMap const& map, PageLayout next);
        InkStroke Relocated(InkStroke const& stroke, int oldPage);
        void EndRelocation() { relocation.reset(); }

        /// The stroke as it is now: undo, redo and relocation put copies in place of strokes (a stroke cannot be added
        /// again), and edits recorded earlier still name the original - they find the copy here.
        InkStroke Current(InkStroke const& stroke) const;
        /// `now` takes `before`'s place (its copy).
        void Replaced(InkStroke const& before, InkStroke const& now) { replacedBy.insert_or_assign(before.Id(), now); }

        int PageOf(InkStroke const& stroke) const;
        void Add(InkStroke const& stroke, int page);
        /// Removes exactly these strokes from the container (by id).
        void Remove(std::vector<InkStroke> const& strokes);
        std::vector<InkStroke> StrokesOn(int page) const;
        std::vector<InkStroke> Find(std::set<uint32_t> const& ids) const;
        /// Whether ink lies within `radius` of a document point (the drawn width counts: the ink's edge, not its
        /// centerline) - the mouse pressing on ink selects it, not the app's content under it.
        bool Touches(PointF point, float radius) const;

        /// The stroke as it goes onto the pages: itself when it lies on one page, else its pieces cut at the pages'
        /// edges (each on its page; nothing of it in the gaps).
        std::vector<Placed> Split(InkStroke const& stroke) const;

    private:
        struct Relocation
        {
            std::vector<PointF> shift;            // by old page
            std::map<uint32_t, InkStroke> copies; // stroke id -> its copy at the new place
            std::set<uint32_t> made;              // the copies' ids: already at their new place
        };
        std::optional<Relocation> relocation;
        std::map<uint32_t, InkStroke> replacedBy; // stroke id -> the copy that took its place (followed in a chain)
    };
}
