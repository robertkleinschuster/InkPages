#pragma once

// Undo/redo of edits: one history in time order across pages (the ink presenter has no undo of its own). Every change
// of the pages' content goes through it, so it also tells an app whether there is something to save (the save point).
// Pure logic over an edit interface (unit-tested with fake edits); the ink's edits are in the library's InkLayer.
// Ported from UndoStack.cs and Model/SavePoint.cs (winui-final).

#include <memory>
#include <set>
#include <vector>

#include "inkpages/model/PageMap.h"

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
        /// Pages were inserted, removed or moved: the edit follows its pages (it never touches a removed one).
        virtual void RemapPages(PageMap const& map) = 0;
    };

    /// Several edits as one step (a drag: strokes and the app's pictures moved, maybe onto another page).
    class CompositeEdit final : public Edit
    {
    public:
        void Add(std::shared_ptr<Edit> edit) { if (edit && !edit->Empty()) edits.push_back(std::move(edit)); }
        std::set<int> Pages() const override;
        bool Empty() const override { return edits.empty(); }
        void Undo() override;
        void Redo() override;
        void RemapPages(PageMap const& map) override;

    private:
        std::vector<std::shared_ptr<Edit>> edits;
    };

    class UndoHistory
    {
    public:
        /// Records an edit that has already been made (empty edits are dropped). Redo is gone.
        void Push(std::shared_ptr<Edit> edit);
        /// `pages`: the pages the undone/redone edit changes.
        bool Undo(std::set<int>* pages = nullptr);
        bool Redo(std::set<int>* pages = nullptr);
        bool CanUndo() const { return !undo.empty(); }
        bool CanRedo() const { return !redo.empty(); }

        /// No history (another document): the content as it is counts as saved.
        void Clear();
        /// Forgets the edits of a removed page; a save point among them cannot be reached any more.
        void Forget(int page);
        /// Pages were inserted, removed or moved: edits of removed pages are forgotten, the others follow their pages.
        void RemapPages(PageMap const& map);

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
