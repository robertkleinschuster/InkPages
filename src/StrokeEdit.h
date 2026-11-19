#pragma once

// An undoable change of the ink: strokes removed and added (by the presenter - collected, erased - or by a tool).
// Recorded after it was made; undo removes the added strokes and puts back copies of the removed ones (a removed stroke
// cannot be added again: its Clone() takes its place, and from then on the copy is what the edit refers to).
// Ink host thread only.

#include "InkState.h"
#include "inkpages/UndoHistory.h"

namespace inkpages::detail
{
    class StrokeEdit final : public Edit
    {
    public:
        explicit StrokeEdit(InkState& ink) : ink(ink) {}

        void Removed(InkStroke const& stroke, int page) { removed.push_back({ stroke, page }); }
        void Added(InkStroke const& stroke, int page) { added.push_back({ stroke, page }); }
        /// A stroke this edit added is gone again (the exact eraser cut it within the same drag).
        bool Forget(InkStroke const& stroke);

        std::set<int> Pages() const override;
        bool Empty() const override { return removed.empty() && added.empty(); }
        void Undo() override;
        void Redo() override;

    private:
        struct Entry
        {
            InkStroke stroke;
            int page;
        };
        static void Swap(InkState& ink, std::vector<Entry>& gone, std::vector<Entry>& back);

        InkState& ink;
        std::vector<Entry> removed, added;
    };
}
