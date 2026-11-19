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
        // Edits recorded earlier may name strokes that undo/redo/relocation have replaced by copies since: the current
        // copy is what goes and what is brought back.
        std::vector<InkStroke> remove;
        for (auto& e : gone)
        {
            e.stroke = ink.Current(e.stroke);
            remove.push_back(e.stroke);
        }
        ink.Remove(remove);
        for (auto& e : back)
        {
            InkStroke was = ink.Current(e.stroke);
            InkStroke copy = was.Clone();
            ink.keys.Copied(was.Id(), copy.Id()); // a stroke brought back keeps the app's key
            ink.Replaced(was, copy);
            ink.Add(copy, e.page);
            e.stroke = copy;
        }
    }

    void StrokeEdit::RemapPages(PageMap const& map)
    {
        for (auto* list : { &removed, &added })
        {
            for (auto& e : *list)
            {
                e.stroke = ink.Relocated(ink.Current(e.stroke), e.page);
                e.page = map(e.page);
            }
        }
    }

    void StrokeEdit::Undo() { Swap(ink, added, removed); }

    void StrokeEdit::Redo() { Swap(ink, removed, added); }
}
