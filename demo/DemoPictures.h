#pragma once

// The demo's own objects on the pages: colored cards and texts (in memory), drawn into the surface's page layers - the
// way an app places its bitmaps and texts - selected, moved and scaled with the strokes through the library's
// PictureHost; texts are written and edited with the Text tool through its TextHost.

#include <map>
#include <optional>

#include <winrt/base.h>

#include "inkpages/WritingSurface.h"

class DemoPictures final : public inkpages::PictureHost, public inkpages::TextHost
{
public:
    explicit DemoPictures(inkpages::WritingSurface& surface) : surface(surface) {}

    /// A card on a page (document coordinates).
    void Add(inkpages::RectF bounds, inkpages::Color color);
    /// Pages were inserted, removed or moved (`before`: the layout before): cards and texts go with their pages (their
    /// visuals are in the pages' layers, which go along); those of a removed page are gone.
    void Remap(inkpages::PageMap const& map, inkpages::PageLayout const& before);

    std::vector<inkpages::PictureId> PicturesIn(int page, std::vector<inkpages::PointF> const& polygon) override;
    std::vector<inkpages::PictureId> PicturesOn(int page) override;
    inkpages::RectF Bounds(inkpages::PictureId picture) override;
    std::optional<inkpages::PictureId> PictureAt(inkpages::PointF point) override;
    void Preview(std::vector<inkpages::PictureId> const& pictures, inkpages::Affine const& m) override;
    void Transform(std::vector<inkpages::PictureId> const& pictures, inkpages::Affine const& m) override;
    uint64_t Remove(std::vector<inkpages::PictureId> const& pictures) override;
    void Restore(uint64_t token) override;
    std::vector<inkpages::PictureId> Duplicate(std::vector<inkpages::PictureId> const& pictures, inkpages::Affine const& m) override;

    std::optional<inkpages::TextId> TextAt(inkpages::PointF point) override;
    inkpages::Text Get(inkpages::TextId text) override;
    void SetEditing(std::optional<inkpages::TextId> text) override;
    inkpages::TextId Add(inkpages::Text const& text) override;
    void Change(inkpages::TextId id, inkpages::Text const& text) override;
    uint64_t Remove(inkpages::TextId id) override { return Remove(std::vector<inkpages::PictureId>{ id }); }

private:
    struct Card
    {
        inkpages::RectF bounds;
        inkpages::Color color;
        int page = 0;
        bool shown = true;
        winrt::com_ptr<IDCompositionVisual> visual;
        std::optional<inkpages::Text> text; // a text (bounds: measured)
    };
    inkpages::PictureId Place(Card card);
    void Measure(Card& card);
    void Relayer(Card& card, int page);
    void Show(Card& card, inkpages::Affine const& preview);
    void Draw(Card& card);

    inkpages::WritingSurface& surface;
    std::optional<inkpages::TextId> editing;
    std::map<inkpages::PictureId, Card> cards;
    std::map<uint64_t, std::vector<inkpages::PictureId>> removed;
    inkpages::PictureId next = 1;
    uint64_t nextToken = 1;
};
