#pragma once

// One sheet below the ink: its color, its paper lines and the app's underlay (background, PDF page, pictures, what
// Windows ink cannot draw), drawn with Direct2D into a DirectComposition virtual surface at the current render scale
// (DPI x zoom) - only the part near the view is drawn (and kept: Trim), so the sheet stays sharp when zoomed: while a
// pinch runs the last drawing is scaled, a settled zoom draws it anew.

#include <d2d1_1.h>
#include <dcomp.h>

#include <functional>

#include <winrt/base.h>

#include "inkpages/model/PageLayout.h"
#include "inkpages/model/Paper.h"

namespace inkpages::detail
{
    struct Graphics;

    /// The app's underlay of a page: draws into the context in page coordinates (its transform maps them to pixels);
    /// `area` is the part being drawn.
    using Underlay = std::function<void(ID2D1DeviceContext* dc, RectF area)>;

    class PaperLayer
    {
    public:
        PaperLayer(Graphics& graphics, RectF page, Paper paper, Underlay underlay);

        IDCompositionVisual* Visual() const { return visual.get(); }

        /// Draws what `needed` (document DIPs) shows of the sheet at `renderScale` pixels per DIP, unless it is drawn
        /// already at that scale. Returns true when it drew.
        bool Update(RectF needed, float renderScale);

        /// The content changed (or a new scale): the next Update draws everything it needs anew.
        void Invalidate() { drawn = {}; }
        void SetPaper(Paper next) { paper = next; Invalidate(); }

        /// The page moved (pages were inserted, removed or moved before it): its drawing goes along, the underlay now
        /// draws for its new index. A new size draws anew.
        void MoveTo(RectF page, Underlay underlay);

        /// Far from the view: free the drawing (the sheet shows nothing until the next Update).
        void Discard();

    private:
        void Draw(RECT pixels);

        Graphics& graphics;
        RectF page;
        Paper paper;
        Underlay underlay;
        winrt::com_ptr<IDCompositionVisual> visual;
        winrt::com_ptr<IDCompositionVirtualSurface> surface;
        float scale = 0;
        RectF drawn{}; // page-relative DIPs drawn at `scale`
    };
}
