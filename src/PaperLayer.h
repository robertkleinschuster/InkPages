#pragma once

// One sheet below the ink: its color and paper lines, drawn with Direct2D into a DirectComposition virtual surface at
// the current render scale (DPI x zoom) - only the part near the view is drawn (and kept: Trim), so the sheet stays
// sharp when zoomed: while a pinch runs the last drawing is scaled, a settled zoom draws it anew.

#include <dcomp.h>

#include <winrt/base.h>

#include "inkpages/PageLayout.h"
#include "inkpages/Paper.h"

namespace inkpages::detail
{
    struct Graphics;

    class PaperLayer
    {
    public:
        PaperLayer(Graphics& graphics, RectF page, Paper paper);

        IDCompositionVisual* Visual() const { return visual.get(); }

        /// Draws what `needed` (document DIPs) shows of the sheet at `renderScale` pixels per DIP, unless it is drawn
        /// already at that scale. Returns true when it drew.
        bool Update(RectF needed, float renderScale);

        /// A new scale: the next Update draws everything it needs anew.
        void Invalidate() { drawn = {}; }

        /// Far from the view: free the drawing (the sheet shows nothing until the next Update).
        void Discard();

    private:
        void Draw(RECT pixels);

        Graphics& graphics;
        RectF page;
        Paper paper;
        winrt::com_ptr<IDCompositionVisual> visual;
        winrt::com_ptr<IDCompositionVirtualSurface> surface;
        float scale = 0;
        RectF drawn{}; // page-relative DIPs drawn at `scale`
    };
}
