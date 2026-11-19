#pragma once

// The demo's "pictures": colored cards, its own objects on the pages, drawn into the surface's page layers - the way an
// app places its bitmaps - and selected, moved and scaled with the strokes through the library's PictureHost.

#include <map>

#include <winrt/base.h>

#include "inkpages/WritingSurface.h"

class DemoPictures final : public inkpages::PictureHost
{
public:
    explicit DemoPictures(inkpages::WritingSurface& surface) : surface(surface) {}

    /// A card on a page (document coordinates).
    void Add(inkpages::RectF bounds, inkpages::Color color);

    std::vector<inkpages::PictureId> PicturesIn(int page, std::vector<inkpages::PointF> const& polygon) override;
    std::vector<inkpages::PictureId> PicturesOn(int page) override;
    inkpages::RectF Bounds(inkpages::PictureId picture) override;
    void Preview(std::vector<inkpages::PictureId> const& pictures, inkpages::Affine const& m) override;
    void Transform(std::vector<inkpages::PictureId> const& pictures, inkpages::Affine const& m) override;
    uint64_t Remove(std::vector<inkpages::PictureId> const& pictures) override;
    void Restore(uint64_t token) override;
    std::vector<inkpages::PictureId> Duplicate(std::vector<inkpages::PictureId> const& pictures, inkpages::Affine const& m) override;

private:
    struct Card
    {
        inkpages::RectF bounds;
        inkpages::Color color;
        int page = 0;
        bool shown = true;
        winrt::com_ptr<IDCompositionVisual> visual;
    };
    void Show(Card& card, inkpages::Affine const& preview);
    void Draw(Card& card);

    inkpages::WritingSurface& surface;
    std::map<inkpages::PictureId, Card> cards;
    std::map<uint64_t, std::vector<inkpages::PictureId>> removed;
    inkpages::PictureId next = 1;
    uint64_t nextToken = 1;
};
