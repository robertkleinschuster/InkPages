#pragma once

// Where each page goes when pages are inserted, removed or moved: the new index of every old page (-1: removed).
// The history, the lazy pages and the strokes follow it, so undo survives page operations (an edit of a removed page
// is dropped - deleting a page cannot be undone, the app asks first). Pure (unit-tested).

#include <set>
#include <vector>

namespace inkpages
{
    struct PageMap
    {
        std::vector<int> newIndex; // by old index
        int newCount = 0;

        static PageMap Insert(int count, int at, int inserted);
        static PageMap Remove(int count, int page);
        static PageMap Move(int count, int from, int to);

        int operator()(int old) const { return old >= 0 && old < static_cast<int>(newIndex.size()) ? newIndex[old] : -1; }
        bool Removes(int old) const { return (*this)(old) < 0; }
        /// The pages' new indices (removed pages left out).
        std::set<int> Apply(std::set<int> const& pages) const;
        /// The old index of every new page (-1: a new page).
        std::vector<int> OldIndex() const;
    };
}
