#pragma once

#include <cstdint>
#include <functional>
#include <set>

#include "inkpages/model/Geometry.h"

namespace inkpages::detail
{
    class InkDragVisual;
    class InkHost;
    class InkLayer;
    class UiThread;

    struct InkDragPreviewContext
    {
        InkHost* host = nullptr;
        UiThread* ui = nullptr;
        std::function<InkLayer*()> ink;                 // ink thread: the presenter's layer (null before it exists)
        InkDragVisual* visual = nullptr;
        std::function<float()> pixelsPerUnit;           // UI thread: device pixels per document unit
        std::function<void(Affine const&)> showFrame;   // UI thread: the selection frame follows the drag
        std::function<void(Affine const&)> moveLive;    // UI thread: no composited ink - the live ink moves instead
        std::function<void()> commit;                   // DirectComposition Commit
    };

    // The ink of a selection while it is moved: composited, whatever its size. When the drag really begins the ink is
    // snapshotted on the ink thread and rendered into textures (InkDragVisual); until that is ready the frame moves and
    // the ink catches up with the drag's latest offset. When the snapshot fails or the pages change, the live ink moves
    // instead (`moveLive`). The ink container is untouched; the app commits the edit when the drag ends.
    class InkDragPreview
    {
    public:
        enum class State { Idle, Pending, Shown, Live };

        explicit InkDragPreview(InkDragPreviewContext context) : c(std::move(context)) {}

        State Current() const { return state; }
        bool Live() const { return state == State::Live; }
        /// The drag's latest offset (translation only). The first call of a drag starts the snapshot of `selected`.
        void Move(std::set<uint32_t> const& selected, Affine const& m);
        /// The pages changed under the drag or the selection: the composited ink is out of date.
        void Invalidate();
        /// The drag ended or was cancelled: a snapshot still being prepared is dropped.
        void End();

    private:
        void Request(std::set<uint32_t> selected);
        void Show();

        InkDragPreviewContext c;
        State state = State::Idle;
        unsigned generation = 0;
        Affine latest;
    };
}
