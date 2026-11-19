#include "surface/Composition.h"

namespace inkpages::detail
{
    Composition::Composition(HWND window)
    {
        auto& dc = graphics.dcomp;
        winrt::check_hresult(dc->CreateTargetForHwnd(window, TRUE, target.put()));
        for (auto* visual : { &root, &content, &sheets, &layers, &ink }) winrt::check_hresult(dc->CreateVisual(visual->put()));
        overlay = std::make_unique<OverlayLayer>(graphics);
        winrt::check_hresult(root->AddVisual(content.get(), FALSE, nullptr));
        winrt::check_hresult(content->AddVisual(sheets.get(), FALSE, nullptr));
        winrt::check_hresult(content->AddVisual(layers.get(), FALSE, nullptr));
        winrt::check_hresult(content->AddVisual(overlay->Visual(), FALSE, nullptr));
        winrt::check_hresult(content->AddVisual(ink.get(), FALSE, nullptr));
        winrt::check_hresult(target->SetRoot(root.get()));
    }

    IDCompositionVisual* Composition::PageLayer(int page) const
    {
        return page >= 0 && page < static_cast<int>(pageLayers.size()) ? pageLayers[page].get() : nullptr;
    }

    void Composition::SetScale(float pixelsPerDip)
    {
        D2D1_MATRIX_3X2_F m{ pixelsPerDip, 0, 0, pixelsPerDip, 0, 0 };
        winrt::check_hresult(content->SetTransform(m));
    }

    void Composition::SetClip(SIZE pixels)
    {
        winrt::com_ptr<IDCompositionRectangleClip> clip;
        winrt::check_hresult(graphics.dcomp->CreateRectangleClip(clip.put()));
        winrt::check_hresult(clip->SetRight(static_cast<float>(pixels.cx)));
        winrt::check_hresult(clip->SetBottom(static_cast<float>(pixels.cy)));
        winrt::check_hresult(root->SetClip(clip.get()));
    }

    void Composition::SetPages(PageLayout const& newLayout, std::vector<Paper> const& paper)
    {
        layout = newLayout;
        papers.clear();
        pageLayers.clear();
        winrt::check_hresult(sheets->RemoveAllVisuals());
        winrt::check_hresult(layers->RemoveAllVisuals());
        for (int i = 0; i < layout.Count(); ++i)
        {
            papers.push_back(std::make_unique<PaperLayer>(graphics, layout.Page(i), paper[i]));
            winrt::check_hresult(sheets->AddVisual(papers.back()->Visual(), FALSE, nullptr));
            winrt::com_ptr<IDCompositionVisual> layer;
            winrt::check_hresult(graphics.dcomp->CreateVisual(layer.put()));
            winrt::check_hresult(layer->SetOffsetX(layout.Page(i).left));
            winrt::check_hresult(layer->SetOffsetY(layout.Page(i).top));
            winrt::check_hresult(layers->AddVisual(layer.get(), FALSE, nullptr));
            pageLayers.push_back(layer);
        }
    }

    void Composition::UpdateSheets(RectF visible, float renderScale)
    {
        float h = visible.Height();
        RectF needed = visible.Inflated(visible.Width() / 2, h / 2);
        for (int i = 0; i < layout.Count(); ++i)
        {
            if (layout.Page(i).Intersects(needed)) papers[i]->Update(needed, renderScale);
            else if (!layout.Page(i).Intersects(visible.Inflated(0, 3 * h))) papers[i]->Discard();
        }
    }

    void Composition::Commit()
    {
        winrt::check_hresult(graphics.dcomp->Commit());
    }
}
