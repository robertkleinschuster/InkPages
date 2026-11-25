#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <set>
#include <vector>

#include "inkpages/Pictures.h"
#include "inkpages/model/Geometry.h"

namespace inkpages::detail
{
    class InkDragVisual;
    struct InkDragObjects;
    class InkHost;
    class InkLayer;
    class UiThread;

    struct InkDragPreviewContext
    {
        InkHost* host = nullptr;
        UiThread* ui = nullptr;
        std::function<InkLayer*()> ink;                 // ink thread: the presenter's layer (null before it exists)
        InkDragVisual* visual = nullptr;
        std::function<PictureHost*()> pictures;         // UI thread: the app's picture host (null: none)
        std::function<float()> pixelsPerUnit;           // UI thread: device pixels per document unit
        std::function<void(Affine const&)> showFrame;   // UI thread: the selection frame follows the drag
        std::function<void(Affine const&)> moveLive;    // UI thread: no composited ink - the live ink moves instead
        std::function<void()> commit;                   // DirectComposition Commit
    };

    // What a selection moves while it is dragged: composited, whatever its size. When the drag really begins the ink is
    // snapshotted on the ink thread and rendered into textures, the app's pictures and texts are drawn into a texture
    // and lifted off their pages (InkDragVisual, PictureHost::DrawLifted/Lift); until that is ready the frame moves and
    // the content catches up with the drag's latest offset. When the snapshot fails or the pages change, the live ink
    // moves instead (`moveLive`) and the pictures show their preview as before. The ink container is untouched; the app
    // commits the edit when the drag ends.
    class InkDragPreview
    {
    public:
        enum class State { Idle, Pending, Shown, Live };

        explicit InkDragPreview(InkDragPreviewContext context) : c(std::move(context)) {}

        State Current() const { return state; }
        bool Live() const { return state == State::Live; }
        /// The drag's latest offset (translation only). The first call of a drag starts the snapshot of the selected
        /// strokes and the texture of the pictures.
        void Move(std::set<uint32_t> const& selected, std::vector<PictureId> const& pictures, Affine const& m);
        /// The pictures are in the texture, off their pages: their own preview is not needed.
        bool ShowsPictures() const { return !lifted.empty() || (state == State::Pending && pending); }
        /// Puts the lifted pictures back on their pages (after they were transformed, or the drag was cancelled).
        void Unlift();
        /// The pages changed under the drag or the selection: the composited ink is out of date.
        void Invalidate();
        /// The drag ended or was cancelled: a snapshot still being prepared is dropped.
        void End();

    private:
        void Request(std::set<uint32_t> selected);
        std::optional<InkDragObjects> ObjectsOf(float pixelsPerUnit);
        void Show();

        InkDragPreviewContext c;
        State state = State::Idle;
        unsigned generation = 0;
        Affine latest;
        std::vector<PictureId> pictures; // the drag's pictures (lifted once the texture is shown)
        std::vector<PictureId> lifted;
        bool pending = false;            // pictures are being prepared
    };
}
