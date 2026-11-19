#include "DemoPictures.h"

#include <d2d1_1.h>
#include <d2d1_1helper.h>

#include <cmath>
#include <format>

#include "inkpages/Log.h"

using namespace inkpages;

namespace
{
    constexpr float Resolution = 3; // pixels per page unit: sharp enough for the demo's zoom range
}

void DemoPictures::Add(RectF bounds, Color color)
{
    Card card{ bounds, color, surface.Layout().NearestPage({ bounds.left, bounds.top }).value_or(0) };
    winrt::check_hresult(surface.CompositionDevice()->CreateVisual(card.visual.put()));
    Draw(card);
    winrt::check_hresult(surface.PageLayer(card.page)->AddVisual(card.visual.get(), FALSE, nullptr));
    auto [it, inserted] = cards.emplace(next++, std::move(card));
    Show(it->second, {});
}

void DemoPictures::Draw(Card& card)
{
    UINT w = static_cast<UINT>(std::ceil(card.bounds.Width() * Resolution)), h = static_cast<UINT>(std::ceil(card.bounds.Height() * Resolution));
    winrt::com_ptr<IDCompositionSurface> drawing;
    winrt::check_hresult(surface.CompositionDevice()->CreateSurface(w, h, DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_ALPHA_MODE_PREMULTIPLIED, drawing.put()));
    winrt::com_ptr<ID2D1DeviceContext> dc;
    POINT offset{};
    winrt::check_hresult(drawing->BeginDraw(nullptr, __uuidof(ID2D1DeviceContext), dc.put_void(), &offset));
    dc->SetTransform(D2D1::Matrix3x2F::Translation(static_cast<float>(offset.x), static_cast<float>(offset.y)));
    winrt::com_ptr<ID2D1SolidColorBrush> fill, border;
    winrt::check_hresult(dc->CreateSolidColorBrush(D2D1::ColorF(card.color.r / 255.f, card.color.g / 255.f, card.color.b / 255.f, 0.85f), fill.put()));
    winrt::check_hresult(dc->CreateSolidColorBrush(D2D1::ColorF(0.2f, 0.2f, 0.2f), border.put()));
    dc->Clear(D2D1::ColorF(0, 0, 0, 0));
    dc->FillRectangle({ 0, 0, static_cast<float>(w), static_cast<float>(h) }, fill.get());
    dc->DrawRectangle({ 1, 1, w - 1.f, h - 1.f }, border.get(), 2);
    winrt::check_hresult(drawing->EndDraw());
    winrt::check_hresult(card.visual->SetContent(drawing.get()));
}

// The card's visual in its page's layer: page coordinates, the (preview) transform applied in document coordinates.
void DemoPictures::Show(Card& card, Affine const& preview)
{
    RectF page = surface.Layout().Page(card.page);
    Affine place = Affine{ 1 / Resolution, 0, 0, 1 / Resolution, card.bounds.left, card.bounds.top }
        .Then(preview).Then(Affine::Translation(-page.left, -page.top));
    D2D1_MATRIX_3X2_F m{ place.m11, place.m12, place.m21, place.m22, place.dx, place.dy };
    winrt::check_hresult(card.visual->SetTransform(m));
    surface.Commit();
}

std::vector<PictureId> DemoPictures::PicturesIn(int page, std::vector<PointF> const& polygon)
{
    std::vector<PictureId> in;
    for (auto& [id, card] : cards)
    {
        if (!card.shown || card.page != page) continue;
        RectF b = card.bounds;
        if (InPolygon(polygon, { b.left, b.top }) && InPolygon(polygon, { b.right, b.top }) &&
            InPolygon(polygon, { b.right, b.bottom }) && InPolygon(polygon, { b.left, b.bottom }))
        {
            in.push_back(id);
        }
    }
    return in;
}

std::vector<PictureId> DemoPictures::PicturesOn(int page)
{
    std::vector<PictureId> on;
    for (auto& [id, card] : cards)
    {
        if (card.shown && card.page == page) on.push_back(id);
    }
    return on;
}

RectF DemoPictures::Bounds(PictureId picture)
{
    auto it = cards.find(picture);
    return it != cards.end() ? it->second.bounds : RectF{};
}

void DemoPictures::Preview(std::vector<PictureId> const& pictures, Affine const& m)
{
    for (PictureId id : pictures)
    {
        if (auto it = cards.find(id); it != cards.end()) Show(it->second, m);
    }
}

void DemoPictures::Transform(std::vector<PictureId> const& pictures, Affine const& m)
{
    for (PictureId id : pictures)
    {
        auto it = cards.find(id);
        if (it == cards.end()) continue;
        Card& card = it->second;
        card.bounds = m.Bounds(card.bounds);
        int page = surface.Layout().NearestPage({ (card.bounds.left + card.bounds.right) / 2, (card.bounds.top + card.bounds.bottom) / 2 }).value_or(0);
        if (page != card.page && card.shown) // onto another page: into its layer
        {
            winrt::check_hresult(surface.PageLayer(card.page)->RemoveVisual(card.visual.get()));
            winrt::check_hresult(surface.PageLayer(page)->AddVisual(card.visual.get(), FALSE, nullptr));
        }
        card.page = page;
        Show(card, {});
    }
    Log(std::format("demo: {} pictures transformed", pictures.size()));
}

uint64_t DemoPictures::Remove(std::vector<PictureId> const& pictures)
{
    for (PictureId id : pictures)
    {
        if (auto it = cards.find(id); it != cards.end() && it->second.shown)
        {
            it->second.shown = false;
            winrt::check_hresult(surface.PageLayer(it->second.page)->RemoveVisual(it->second.visual.get()));
        }
    }
    surface.Commit();
    removed[nextToken] = pictures;
    return nextToken++;
}

void DemoPictures::Restore(uint64_t token)
{
    for (PictureId id : removed[token])
    {
        if (auto it = cards.find(id); it != cards.end() && !it->second.shown)
        {
            it->second.shown = true;
            winrt::check_hresult(surface.PageLayer(it->second.page)->AddVisual(it->second.visual.get(), FALSE, nullptr));
        }
    }
    removed.erase(token);
    surface.Commit();
}

std::vector<PictureId> DemoPictures::Duplicate(std::vector<PictureId> const& pictures, Affine const& m)
{
    std::vector<PictureId> copies;
    for (PictureId id : pictures)
    {
        auto it = cards.find(id);
        if (it == cards.end()) continue;
        RectF b = m.Bounds(it->second.bounds);
        Color c = it->second.color;
        copies.push_back(next);
        Add(b, c);
    }
    return copies;
}
