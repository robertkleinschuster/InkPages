#pragma once

// The surface's DirectComposition tree (UI thread):
//   root (clipped to the window) -> [DirectManipulation's visuals] -> content (DIPs -> pixels) ->
//     marks (the outline of the page in view) -> sheets (paper) -> page layers (the app's content) -> texts (the edited text box) -> overlay (tools' feedback) ->
//     ink (the presenter's root)
// AddVisual(v, FALSE, nullptr) puts v above its siblings: the ink is on top - measured: under an opaque sheet it got no
// pen input. Sheets and the overlay are drawn at the settled zoom's resolution, near the view only.

#include <memory>
#include <vector>

#include "inkpages/model/PageLayout.h"
#include "inkpages/model/PageMap.h"
#include "inkpages/model/Paper.h"
#include "render/Graphics.h"
#include "render/Overlay.h"
#include "render/PaperLayer.h"

namespace inkpages::detail
{
    class Composition
    {
    public:
        explicit Composition(HWND window);

        Graphics& Devices() { return graphics; }
        IDCompositionVisual* Root() const { return root.get(); }
        IDCompositionVisual* Content() const { return content.get(); }
        IDCompositionVisual* Ink() const { return ink.get(); }
        IDCompositionVisual* PageLayer(int page) const;
        IDCompositionVisual* TextLayer() const { return texts.get(); }
        IDCompositionVisual* MarkLayer() const { return marks.get(); }
        OverlayLayer& Overlay() { return *overlay; }

        void SetScale(float pixelsPerDip);
        void SetClip(SIZE pixels);
        /// `underlay(page, dc, area)`: the app's drawing under the ink of a page (page coordinates).
        void SetPages(PageLayout const& layout, std::vector<Paper> const& papers,
            std::function<void(int, ID2D1DeviceContext*, RectF)> underlay);
        /// Pages were inserted, removed or moved: new sheets; the page layers (the app's content) go with their pages.
        void Rearrange(PageLayout const& layout, std::vector<Paper> const& papers, PageMap const& map,
            std::function<void(int, ID2D1DeviceContext*, RectF)> underlay);
        /// A page's underlay changed: drawn anew at the next UpdateSheets.
        void Invalidate(int page);
        /// Draws the sheets near `visible` at `renderScale` (pixels per page unit); far sheets free their drawing.
        void UpdateSheets(RectF visible, float renderScale);
        void Commit();

    private:
        Graphics graphics;
        winrt::com_ptr<IDCompositionTarget> target;
        winrt::com_ptr<IDCompositionVisual> root, content, marks, sheets, layers, texts, ink;
        std::unique_ptr<OverlayLayer> overlay;
        std::vector<std::unique_ptr<PaperLayer>> papers;
        std::vector<winrt::com_ptr<IDCompositionVisual>> pageLayers;
        PageLayout layout;
    };
}
