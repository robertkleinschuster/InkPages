#pragma once

// One text edit in the shared history (a text added, changed or emptied away). The history runs on the ink thread, the
// texts belong to the app's UI thread: undo and redo hand the work over.

#include <functional>
#include <memory>
#include <set>

#include "ink/UiThread.h"
#include "inkpages/Texts.h"
#include "inkpages/model/UndoHistory.h"

namespace inkpages::detail
{
    class TextEdit final : public Edit
    {
    public:
        enum class Kind { Added, Changed, Removed };

        /// `host` returns the app's text host at the time of undo/redo (UI thread; none: nothing happens). `token`: of
        /// the removal (Removed).
        TextEdit(UiThread& ui, std::function<TextHost*()> host, Kind kind, TextId id, Text before, Text after,
            uint64_t token = 0);

        std::set<int> Pages() const override { return { before.page, after.page }; }
        bool Empty() const override { return false; }
        void Undo() override { Run(true); }
        void Redo() override { Run(false); }
        void RemapPages(PageMap const& map) override;

    private:
        void Run(bool undo);

        UiThread& ui;
        std::function<TextHost*()> host;
        Kind kind;
        TextId id;
        Text before, after;
        std::shared_ptr<uint64_t> token; // renewed by the UI-thread work
    };
}
