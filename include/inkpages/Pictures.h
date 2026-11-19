#pragma once

// The app's own objects on the pages (placed bitmaps, text boxes, ...): the library selects, moves, scales, deletes,
// duplicates and copies them together with the strokes, and undoes that. The app draws them (in the surface's page
// layers) and keeps them. All calls on the UI thread, in document coordinates (page units from the document's top left).

#include <cstdint>
#include <optional>
#include <vector>

#include "inkpages/model/Geometry.h"

namespace inkpages
{
    using PictureId = uint64_t;

    class PictureHost
    {
    public:
        virtual ~PictureHost() = default;

        /// The pictures of a page entirely inside the polygon (a lasso or rectangle selection).
        virtual std::vector<PictureId> PicturesIn(int page, std::vector<PointF> const& polygon) = 0;
        /// All pictures of a page (select all).
        virtual std::vector<PictureId> PicturesOn(int page) = 0;
        virtual RectF Bounds(PictureId picture) = 0;

        /// While a selection is dragged: show the pictures transformed by `m` (identity: as they are).
        virtual void Preview(std::vector<PictureId> const& pictures, Affine const& m) = 0;
        /// A drag ended (or its undo/redo): transform the pictures by `m`. One that lands on another page belongs to it.
        virtual void Transform(std::vector<PictureId> const& pictures, Affine const& m) = 0;

        /// Takes pictures off their pages (delete, cut, undo of an addition); the token puts them back.
        virtual uint64_t Remove(std::vector<PictureId> const& pictures) = 0;
        virtual void Restore(uint64_t token) = 0;
        /// Copies of the pictures transformed by `m`, added to the pages (duplicate).
        virtual std::vector<PictureId> Duplicate(std::vector<PictureId> const& pictures, Affine const& m) = 0;
    };
}
