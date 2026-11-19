#pragma once

// Undo/redo of edits: one history in time order across pages (the ink presenter has no undo of its own). Every change
// of the pages' content goes through it, so it also tells an app whether there is something to save (the save point).
// Pure logic over an edit interface (unit-tested with fake edits); the ink's edits are in the library's InkLayer.
// Ported from UndoStack.cs and Model/SavePoint.cs (winui-final).

#include <memory>
#include <set>
#include <vector>

namespace inkpages
{
    class Edit
    {
    public:
        virtual ~Edit() = default;
        virtual std::set<int> Pages() const = 0; // the pages it changes (their ink must stay while it is in the history)
        virtual bool Empty() const = 0;
        virtual void Undo() = 0;
        virtual void Redo() = 0;
    };

    class UndoHistory
    {
    public:
        /// Records an edit that has already been made (empty edits are dropped). Redo is gone.
        void Push(std::shared_ptr<Edit> edit);
        bool Undo();
        bool Redo();
        bool CanUndo() const { return !undo.empty(); }
        bool CanRedo() const { return !redo.empty(); }

        /// No history (another document): the content as it is counts as saved.
        void Clear();
        /// Forgets the edits of a removed page; a save point among them cannot be reached any more.
        void Forget(int page);

        /// The pages any edit to undo or redo changes.
        std::set<int> Pages() const;

        /// The content as it is now was saved (or loaded); whether it is still (or again, after undoing back) so.
        void MarkSaved() { saved = Top(); reachable = true; }
        bool AtSavePoint() const { return reachable && saved == Top(); }

    private:
        std::shared_ptr<Edit> Top() const { return undo.empty() ? nullptr : undo.back(); }
        std::vector<std::shared_ptr<Edit>> undo, redo;
        std::shared_ptr<Edit> saved; // kept alive: a later edit can never be taken for it
        bool reachable = true;
    };
}
