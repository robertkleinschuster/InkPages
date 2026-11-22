#pragma once

// The selection on the UI thread: the selected strokes (the ink thread keeps them) and the app's pictures, its frame,
// moving, scaling and rotating it with window pointer input (the pen over the selection is given to the window; the
// mouse always is), auto-scroll near the view's edge, and its commands. Every change of
// content is one undo step (strokes and pictures together). Moving and scaling are exact - only a tap is not a move.

#include <windows.h>

#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

#include "ink/InkHost.h"
#include "ink/UiThread.h"
#include "inkpages/WritingSurface.h"
#include "surface/Viewport.h"
#include "tools/InkSelection.h"

namespace inkpages::detail
{
    class ToolInput;
    class InkLayer;
    class InkDragVisual;

    struct SelectionContext
    {
        HWND window = nullptr;
        Viewport* viewport = nullptr;
        InkHost* host = nullptr;
        UiThread* ui = nullptr;
        std::function<InkLayer*()> ink;              // ink thread: the presenter's layer (null before it exists)
        std::function<ToolInput*()> tools;           // ink thread: the tools (null before the presenter exists)
        std::function<PictureHost*()> pictures;      // UI thread: the app's picture host (null: none)
        std::function<float()> scale;                // pixels per DIP
        std::function<PageLayout const&()> layout;
        std::function<void()> changed;               // the selection changed
        std::function<bool(PointF document)> keepsPen; // the window keeps the pen there too (the edited text box)
        WritingSurface* surface = nullptr;           // the app's clipboard callbacks
        InkDragVisual* dragInk = nullptr;
        std::function<void(SelectionFrame)> previewFrame;
        std::function<void()> commit;
    };

    class SelectionController
    {
    public:
        explicit SelectionController(SelectionContext context) : c(std::move(context)) {}

        bool HasSelection() const { return !strokes.Empty() || !pictures.empty(); }
        /// A press at this document point would move or scale the selection (it is on the frame).
        bool Grabs(PointF document) const { return frame && frame->At(document) != Handle::None; }
        LPCWSTR CursorAt(PointF document) const;
        /// The view zoomed: the frame and its handles keep their size on the screen.
        void Zoomed();

        // From the ink thread's tools (handed to the UI thread):
        void Selected(StrokeSelection const& made, std::vector<PointF> const& polygon);
        void Cleared();
        /// A click (mouse) selects the app's picture under it, as a selection tap does; false when there is none.
        bool SelectPictureAt(PointF at);

        /// Window pointer input (pen given to the window, mouse); true when it was the selection's.
        bool OnPointer(UINT message, WPARAM wp, LPARAM lp);
        bool OnKey(WPARAM key);
        void BeginTouchDrag(POINT client);
        void UpdateTouchDrag(POINT client);
        void EndTouchDrag(bool cancelled);
        /// The auto-scroll timer of a drag near the view's edge.
        void OnScrollTimer();
        void CancelDrag();
        void InvalidateInkPreview();

        void Clear();
        void Cut();
        bool Copy(); // false: nothing copied (no selection, or the app's copy failed)
        void Paste();
        void PasteAt(PointF at);
        void Duplicate();
        void Delete();
        void SelectAll(int page);

    private:
        struct Preview
        {
            std::mutex mutex;
            std::optional<Affine> pending;
        };

        struct Drag
        {
            Handle handle = Handle::None;
            PointF start;     // document
            POINT client{};         // the pointer, window pixels (auto-scroll keeps the document point under it)
            float farthest = 0;
            bool barrel = false;
            Affine m;
            std::shared_ptr<Preview> preview;
        };

        PointF ToDocument(POINT client) const;
        void Press(POINT client);
        void MoveTo(POINT client);
        void Release(POINT client);
        void ResetPreview();
        void PostPreview(Affine m);
        void DiscardPendingPreview();
        void TransformSelection(Affine m);
        void Set(StrokeSelection const& made, std::vector<PictureId> picked);
        void Publish(bool log = true);
        std::optional<SelectionFrame> FrameAround(RectF bounds) const;
        void PushWith(std::function<std::shared_ptr<Edit>(ToolInput&)> strokes, std::shared_ptr<Edit> pictureEdit,
            bool keep);
        std::shared_ptr<Edit> PictureStep(int kind, std::vector<PictureId> ids, Affine m, uint64_t token, std::set<int> pages);
        std::set<int> PicturePages(std::vector<PictureId> const& ids) const;

        SelectionContext c;
        StrokeSelection strokes;
        std::vector<PictureId> pictures;
        std::optional<SelectionFrame> frame;
        float frameZoom = 0; // the zoom the frame was laid out for
        std::optional<Drag> drag;
        unsigned previewGeneration = 0;
        void PrepareInkPreview();
    };
}
