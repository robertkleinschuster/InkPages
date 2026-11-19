#include "ink/History.h"

#include "inkpages/Log.h"

namespace inkpages::detail
{
    void History::Push(std::shared_ptr<Edit> edit)
    {
        if (!edit || edit->Empty()) return;
        auto touched = edit->Pages();
        history.Push(std::move(edit));
        Publish(std::move(touched));
    }

    void History::Undo()
    {
        if (!history.CanUndo()) return;
        Log("ink: undo");
        std::set<int> pages;
        history.Undo(&pages);
        Publish(std::move(pages));
    }

    void History::Redo()
    {
        if (!history.CanRedo()) return;
        Log("ink: redo");
        std::set<int> pages;
        history.Redo(&pages);
        Publish(std::move(pages));
    }

    void History::MarkSaved()
    {
        history.MarkSaved();
        Publish({});
    }

    void History::Clear()
    {
        history.Clear();
        Publish({});
    }

    void History::RemapPages(PageMap const& map)
    {
        history.RemapPages(map);
        Publish({});
    }

    HistoryState History::State() const
    {
        std::scoped_lock guard(lock);
        return state;
    }

    void History::Publish(std::set<int> touched)
    {
        HistoryState now{ history.CanUndo(), history.CanRedo(), history.AtSavePoint(), history.Pages(), std::move(touched) };
        {
            std::scoped_lock guard(lock);
            state = now;
        }
        if (changed) changed(std::move(now));
    }
}
