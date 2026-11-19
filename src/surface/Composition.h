#pragma once

// The surface's DirectComposition tree (UI thread):
//   root (clipped to the window) -> [DirectManipulation's visuals] -> content (DIPs -> pixels) ->
//     sheets (paper) -> page layers (the app's content) -> overlay (tools' feedback) -> ink (the presenter's root)
// AddVisual(v, FALSE, nullptr) puts v above its siblings: the ink is on top - measured: under an opaque sheet it got no
// pen input. Sheets and the overlay are drawn at the settled zoom's resolution, near the view only.

#include <memory>
#include <vector>

#include "inkpages/model/PageLayout.h"
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
        OverlayLayer& Overlay() { return *overlay; }

        void SetScale(float pixelsPerDip);
        void SetClip(SIZE pixels);
        void SetPages(PageLayout const& layout, std::vector<Paper> const& papers);
        /// Draws the sheets near `visible` at `renderScale` (pixels per page unit); far sheets free their drawing.
        void UpdateSheets(RectF visible, float renderScale);
        void Commit();

    private:
        Graphics graphics;
        winrt::com_ptr<IDCompositionTarget> target;
        winrt::com_ptr<IDCompositionVisual> root, content, sheets, layers, ink;
        std::unique_ptr<OverlayLayer> overlay;
        std::vector<std::unique_ptr<PaperLayer>> papers;
        std::vector<winrt::com_ptr<IDCompositionVisual>> pageLayers;
        PageLayout layout;
    };
}
