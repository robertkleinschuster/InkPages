#include "ink/StrokeEdit.h"

#include <algorithm>

namespace inkpages::detail
{
    bool StrokeEdit::Forget(InkStroke const& stroke)
    {
        auto it = std::find_if(added.begin(), added.end(), [&](Entry const& e) { return e.stroke.Id() == stroke.Id(); });
        if (it == added.end()) return false;
        added.erase(it);
        return true;
    }

    std::set<int> StrokeEdit::Pages() const
    {
        std::set<int> pages;
        for (auto const& e : removed) pages.insert(e.page);
        for (auto const& e : added) pages.insert(e.page);
        return pages;
    }

    void StrokeEdit::Swap(InkState& ink, std::vector<Entry>& gone, std::vector<Entry>& back)
    {
        std::vector<InkStroke> remove;
        for (auto const& e : gone) remove.push_back(e.stroke);
        ink.Remove(remove);
        for (auto& e : back)
        {
            InkStroke copy = e.stroke.Clone();
            ink.keys.Copied(e.stroke.Id(), copy.Id()); // a stroke brought back keeps the app's key
            ink.Add(copy, e.page);
            e.stroke = copy;
        }
    }

    void StrokeEdit::Undo() { Swap(ink, added, removed); }

    void StrokeEdit::Redo() { Swap(ink, removed, added); }
}
