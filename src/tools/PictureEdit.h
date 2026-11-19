#pragma once

// The app's pictures in an undo step (moved/scaled, removed, added). The history runs on the ink thread, the pictures
// belong to the UI thread: undo and redo hand the work over.

#include <functional>
#include <memory>
#include <set>
#include <vector>

#include "ink/UiThread.h"
#include "inkpages/Pictures.h"
#include "inkpages/model/UndoHistory.h"

namespace inkpages::detail
{
    class PictureEdit final : public Edit
    {
    public:
        enum class Kind { Transformed, Removed, Added };

        /// `host` returns the app's picture host at the time of undo/redo (UI thread; may be gone: then nothing happens).
        PictureEdit(UiThread& ui, std::function<PictureHost*()> host, Kind kind, std::vector<PictureId> pictures,
            std::set<int> pages, Affine m = {}, uint64_t token = 0);

        std::set<int> Pages() const override { return pages; }
        bool Empty() const override { return pictures.empty(); }
        void Undo() override;
        void Redo() override;

    private:
        void Run(bool undo);

        UiThread& ui;
        std::function<PictureHost*()> host;
        Kind kind;
        std::vector<PictureId> pictures;
        std::set<int> pages;
        Affine m;
        std::shared_ptr<uint64_t> token; // shared with the UI-thread work that renews it
    };
}
