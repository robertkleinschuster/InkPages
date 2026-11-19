#include "inkpages/UndoHistory.h"

#include <algorithm>

namespace inkpages
{
    void UndoHistory::Push(std::shared_ptr<Edit> edit)
    {
        if (!edit || edit->Empty()) return;
        undo.push_back(std::move(edit));
        redo.clear();
    }

    bool UndoHistory::Undo()
    {
        if (undo.empty()) return false;
        auto edit = undo.back();
        undo.pop_back();
        edit->Undo();
        redo.push_back(std::move(edit));
        return true;
    }

    bool UndoHistory::Redo()
    {
        if (redo.empty()) return false;
        auto edit = redo.back();
        redo.pop_back();
        edit->Redo();
        undo.push_back(std::move(edit));
        return true;
    }

    void UndoHistory::Clear()
    {
        undo.clear();
        redo.clear();
        saved = nullptr;
        reachable = true;
    }

    void UndoHistory::Forget(int page)
    {
        auto touches = [page](std::shared_ptr<Edit> const& e) { return e->Pages().contains(page); };
        std::erase_if(undo, touches);
        std::erase_if(redo, touches);
        if (saved && std::find(undo.begin(), undo.end(), saved) == undo.end())
        {
            reachable = false; // that state cannot come back by undo any more
        }
    }

    std::set<int> UndoHistory::Pages() const
    {
        std::set<int> pages;
        for (auto const* list : { &undo, &redo })
        {
            for (auto const& e : *list)
            {
                auto p = e->Pages();
                pages.insert(p.begin(), p.end());
            }
        }
        return pages;
    }
}
