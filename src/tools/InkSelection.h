#pragma once

// The selected strokes and what can be done with them (ink thread): select inside a lasso or rectangle (the
// container's SelectWithPolyLine), preview a move/scale (InkStroke.PointTransform: the ink redraws it live), commit it
// (transformed strokes take the originals' place: the pen width stays; on another page they belong to that page),
// delete, duplicate, copy (Windows ink's clipboard or copies for the app's clipboard), add pasted strokes.

#include <memory>
#include <optional>
#include <set>
#include <vector>

#include "ink/InkState.h"
#include "ink/StrokeEdit.h"

namespace inkpages::detail
{
    struct StrokeSelection
    {
        std::set<uint32_t> ids;
        RectF bounds; // with the strokes' width
        bool Empty() const { return ids.empty(); }
    };

    class InkSelection
    {
    public:
        explicit InkSelection(InkState& ink) : ink(ink) {}

        StrokeSelection Select(std::vector<PointF> const& polygon);
        StrokeSelection SelectAt(PointF point, float radius);
        StrokeSelection SelectAll(int page);
        StrokeSelection Current() const { return selection; }
        void Clear();

        void Preview(Affine const& m);
        /// The selection moved/scaled by `m`, as one edit; the moved strokes become the selection.
        std::shared_ptr<StrokeEdit> Commit(Affine const& m);
        std::shared_ptr<StrokeEdit> Delete();
        /// Copies moved by `m`; they become the selection.
        std::shared_ptr<StrokeEdit> Duplicate(Affine const& m);
        /// Copies relative to `origin` (for the app's clipboard).
        std::vector<InkStroke> Copies(PointF origin) const;
        /// Strokes relative to `at` added to the pages; they become the selection.
        std::shared_ptr<StrokeEdit> Add(std::vector<InkStroke> const& relative, PointF at);
        bool CopyToClipboard();
        /// Windows ink's clipboard pasted at `at`; the pasted strokes become the selection.
        std::shared_ptr<StrokeEdit> PasteFromClipboard(PointF at);

    private:
        std::vector<InkStroke> Strokes() const { return ink.Find(selection.ids); }
        /// Adds strokes (cut at the pages' edges) to the edit and makes them the selection.
        void Place(std::vector<InkStroke> const& strokes, StrokeEdit& edit);
        void Measure();

        InkState& ink;
        StrokeSelection selection;
    };
}
