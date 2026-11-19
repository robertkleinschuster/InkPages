#include "DemoPictures.h"

#include <d2d1_1.h>
#include <d2d1_1helper.h>
#include <dwrite.h>

#include <cmath>
#include <format>

#include "inkpages/Log.h"

using namespace inkpages;

namespace
{
    constexpr float Resolution = 3; // pixels per page unit: sharp enough for the demo's zoom range

    IDWriteFactory* DWrite()
    {
        static winrt::com_ptr<IDWriteFactory> factory = [] {
            winrt::com_ptr<IDWriteFactory> f;
            winrt::check_hresult(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(f.put())));
            return f;
        }();
        return factory.get();
    }

    // A text laid out as the demo draws it: its font size in page units (as RichEdit edits it), no wrapping.
    winrt::com_ptr<IDWriteTextLayout> LayoutOf(Text const& text, float pixelsPerUnit)
    {
        winrt::com_ptr<IDWriteTextFormat> format;
        winrt::check_hresult(DWrite()->CreateTextFormat(text.style.font.c_str(), nullptr, DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, text.style.size * pixelsPerUnit, L"de-de", format.put()));
        winrt::check_hresult(format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP));
        winrt::com_ptr<IDWriteTextLayout> layout;
        winrt::check_hresult(DWrite()->CreateTextLayout(text.content.c_str(), static_cast<UINT32>(text.content.size()),
            format.get(), 100000, 100000, layout.put()));
        return layout;
    }
}

void DemoPictures::Add(RectF bounds, Color color)
{
    Place(Card{ bounds, color, surface.Layout().NearestPage({ bounds.left, bounds.top }).value_or(0) });
}

void DemoPictures::Remap(PageMap const& map, PageLayout const& before)
{
    for (auto it = cards.begin(); it != cards.end();)
    {
        Card& card = it->second;
        int to = map(card.page);
        if (to < 0) // its page is gone (its layer with it)
        {
            it = cards.erase(it);
            continue;
        }
        RectF from = before.Page(card.page), now = surface.Layout().Page(to);
        float dx = now.left - from.left, dy = now.top - from.top;
        card.bounds = { card.bounds.left + dx, card.bounds.top + dy, card.bounds.right + dx, card.bounds.bottom + dy };
        card.page = to;
        if (card.text) card.text->page = to;
        Show(card, {});
        ++it;
    }
}

PictureId DemoPictures::Place(Card card)
{
    Measure(card);
    winrt::check_hresult(surface.CompositionDevice()->CreateVisual(card.visual.put()));
    Draw(card);
    winrt::check_hresult(surface.PageLayer(card.page)->AddVisual(card.visual.get(), FALSE, nullptr));
    PictureId id = next++;
    auto [it, inserted] = cards.emplace(id, std::move(card));
    Show(it->second, {});
    return id;
}

// A text's bounds: where it lies (its top left) and what its layout takes.
void DemoPictures::Measure(Card& card)
{
    if (!card.text) return;
    DWRITE_TEXT_METRICS metrics{};
    winrt::check_hresult(LayoutOf(*card.text, 1)->GetMetrics(&metrics));
    RectF page = surface.Layout().Page(card.text->page);
    float left = page.left + card.text->at.x, top = page.top + card.text->at.y;
    card.bounds = { left, top, left + std::max(metrics.widthIncludingTrailingWhitespace, 1.f), top + std::max(metrics.height, 1.f) };
    card.page = card.text->page;
}

void DemoPictures::Relayer(Card& card, int page)
{
    if (page != card.page && card.shown)
    {
        winrt::check_hresult(surface.PageLayer(card.page)->RemoveVisual(card.visual.get()));
        winrt::check_hresult(surface.PageLayer(page)->AddVisual(card.visual.get(), FALSE, nullptr));
    }
    card.page = page;
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
    if (card.text)
    {
        Color c = card.text->style.color;
        winrt::com_ptr<ID2D1SolidColorBrush> ink;
        winrt::check_hresult(dc->CreateSolidColorBrush(D2D1::ColorF(c.r / 255.f, c.g / 255.f, c.b / 255.f), ink.put()));
        dc->DrawTextLayout({ 0, 0 }, LayoutOf(*card.text, Resolution).get(), ink.get());
        winrt::check_hresult(drawing->EndDraw());
        winrt::check_hresult(card.visual->SetContent(drawing.get()));
        return;
    }
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

std::optional<PictureId> DemoPictures::PictureAt(PointF point)
{
    std::optional<PictureId> top; // the last added lies on top
    for (auto& [id, card] : cards)
    {
        RectF b = card.bounds;
        if (card.shown && point.x >= b.left && point.x <= b.right && point.y >= b.top && point.y <= b.bottom) top = id;
    }
    return top;
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
        RectF before = card.bounds;
        card.bounds = m.Bounds(card.bounds);
        int page = surface.Layout().NearestPage({ (card.bounds.left + card.bounds.right) / 2, (card.bounds.top + card.bounds.bottom) / 2 }).value_or(0);
        Relayer(card, page); // onto another page: into its layer
        if (card.text) // a text keeps its top left; scaling scales its size
        {
            RectF r = surface.Layout().Page(page);
            card.text->page = page;
            card.text->at = { card.bounds.left - r.left, card.bounds.top - r.top };
            if (before.Width() > 0) card.text->style.size *= card.bounds.Width() / before.Width();
            Measure(card);
            Draw(card);
        }
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
        Card copy{ m.Bounds(it->second.bounds), it->second.color, it->second.page };
        copy.page = surface.Layout().NearestPage({ copy.bounds.left, copy.bounds.top }).value_or(0);
        if (auto text = it->second.text)
        {
            RectF r = surface.Layout().Page(copy.page);
            text->page = copy.page;
            text->at = { copy.bounds.left - r.left, copy.bounds.top - r.top };
            copy.text = text;
        }
        copies.push_back(Place(std::move(copy)));
    }
    return copies;
}

std::optional<TextId> DemoPictures::TextAt(PointF point)
{
    std::optional<TextId> top;
    for (auto& [id, card] : cards)
    {
        RectF b = card.bounds;
        if (card.text && card.shown && point.x >= b.left && point.x <= b.right && point.y >= b.top && point.y <= b.bottom) top = id;
    }
    return top;
}

Text DemoPictures::Get(TextId id)
{
    auto it = cards.find(id);
    return it != cards.end() && it->second.text ? *it->second.text : Text{};
}

// The edited text is the library's to show meanwhile: its visual is hidden.
void DemoPictures::SetEditing(std::optional<TextId> text)
{
    auto show = [this](std::optional<TextId> id, bool shown) {
        if (!id) return;
        if (auto it = cards.find(*id); it != cards.end()) winrt::check_hresult(it->second.visual.as<IDCompositionVisual3>()->SetOpacity(shown ? 1.f : 0.f));
    };
    show(editing, true);
    editing = text;
    show(editing, false);
    surface.Commit();
    Log(std::format("demo: editing text {}", text ? std::to_string(*text) : std::string("none")));
}

TextId DemoPictures::Add(Text const& text)
{
    Card card{ {}, text.style.color, text.page };
    card.text = text;
    TextId id = Place(std::move(card));
    Log(std::format("demo: text {} added on page {} ({} characters)", id, text.page, text.content.size()));
    return id;
}

void DemoPictures::Change(TextId id, Text const& text)
{
    auto it = cards.find(id);
    if (it == cards.end()) return;
    Card& card = it->second;
    Relayer(card, text.page);
    card.text = text;
    Measure(card);
    Draw(card);
    Show(card, {});
    Log(std::format("demo: text {} changed ({} characters)", id, text.content.size()));
}
