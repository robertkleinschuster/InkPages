#pragma once

// Which app stroke an ink stroke is: the app gives each loaded stroke a key (e.g. its file element) and gets it back on
// save, so it can write unchanged strokes back as they were. The ink replaces stroke objects all the time (a copy at
// a page's place, Clone() in undo, a moved stroke rebuilt), so the library follows the key from object to object, with
// the transform a move or scale applied (the pen width is never scaled). New ink, erased pieces and strokes cut at a
// page's edge have no key (0). Pure logic over ink stroke ids (unit-tested).

#include <cstdint>
#include <map>
#include <optional>

#include "inkpages/model/Geometry.h"

namespace inkpages
{
    struct StrokeKey
    {
        uint64_t key = 0;
        /// From the stroke's points as loaded to its points now, in the coordinates of the page it was loaded on
        /// (origin = that page's top left) and the page it is on now.
        Affine transform;
    };

    class StrokeKeys
    {
    public:
        /// A loaded stroke (now ink stroke `id`) on the page with top left `pageOrigin` (document coordinates).
        void Loaded(uint32_t id, uint64_t key, Affine transform, PointF pageOrigin);
        /// A copy took the stroke's place (Clone in undo/redo, a copy at another place in the presenter).
        void Copied(uint32_t from, uint32_t to);
        /// The stroke was rebuilt moved/scaled by `m` (document coordinates).
        void Moved(uint32_t from, uint32_t to, Affine const& m);
        void Forget(uint32_t id);

        /// The key of stroke `id` as the page with top left `pageOrigin` holds it now (no key: none).
        std::optional<StrokeKey> Find(uint32_t id, PointF pageOrigin) const;

    private:
        struct Entry
        {
            uint64_t key;
            Affine document; // loaded document points -> current document points
            PointF loadedOrigin;
        };
        std::map<uint32_t, Entry> entries;
    };
}
