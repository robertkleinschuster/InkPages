#include "inkpages/model/StrokeKeys.h"

namespace inkpages
{
    void StrokeKeys::Loaded(uint32_t id, uint64_t key, Affine transform, PointF pageOrigin)
    {
        if (key == 0) return; // new ink
        // The app's transform is in page coordinates (the stroke's page): in document coordinates around that page.
        Affine toPage = Affine::Translation(-pageOrigin.x, -pageOrigin.y);
        Affine toDocument = Affine::Translation(pageOrigin.x, pageOrigin.y);
        entries[id] = { key, toPage.Then(transform).Then(toDocument), pageOrigin };
    }

    void StrokeKeys::Copied(uint32_t from, uint32_t to)
    {
        if (auto it = entries.find(from); it != entries.end()) entries[to] = it->second;
    }

    void StrokeKeys::Moved(uint32_t from, uint32_t to, Affine const& m)
    {
        auto it = entries.find(from);
        if (it == entries.end()) return;
        Entry moved = it->second;
        moved.document = moved.document.Then(m);
        entries[to] = moved;
    }

    void StrokeKeys::Forget(uint32_t id)
    {
        entries.erase(id);
    }

    std::optional<StrokeKey> StrokeKeys::Find(uint32_t id, PointF pageOrigin) const
    {
        auto it = entries.find(id);
        if (it == entries.end()) return std::nullopt;
        Entry const& e = it->second;
        Affine fromLoadedPage = Affine::Translation(e.loadedOrigin.x, e.loadedOrigin.y);
        Affine toPage = Affine::Translation(-pageOrigin.x, -pageOrigin.y);
        return StrokeKey{ e.key, fromLoadedPage.Then(e.document).Then(toPage) };
    }
}
