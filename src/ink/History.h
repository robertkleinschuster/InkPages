#pragma once

// The undo history of the ink thread, and a copy of its state for the UI thread (enable undo/redo, keep pages with
// history, the save point).

#include <functional>
#include <memory>
#include <mutex>
#include <set>

#include "inkpages/model/UndoHistory.h"

namespace inkpages::detail
{
    struct HistoryState
    {
        bool canUndo = false, canRedo = false, atSavePoint = true;
        std::set<int> pages; // pages with history: their ink must stay
        std::set<int> changed; // the pages the last change touched
    };

    class History
    {
    public:
        /// Ink thread: after every change, with its state (the owner hands it to the UI thread).
        std::function<void(HistoryState)> changed;

        void Push(std::shared_ptr<Edit> edit);
        void Undo();
        void Redo();
        void MarkSaved();
        void Clear();
        bool Holds(int page) const { return history.Pages().contains(page); }

        HistoryState State() const;

    private:
        void Publish(std::set<int> touched);

        UndoHistory history;
        mutable std::mutex lock;
        HistoryState state;
    };
}
