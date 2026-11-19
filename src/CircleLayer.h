#pragma once

// The exact eraser's circle at the pen: a thin gray ring drawn with Direct2D into a small DirectComposition surface on
// the sheets, below the ink (a visual above the ink would sit in the pen's way). Its outer edge is where erasing ends.
// UI thread.

#include <dcomp.h>

#include <winrt/base.h>

#include "inkpages/Paper.h"

namespace inkpages::detail
{
    struct Graphics;

    class CircleLayer
    {
    public:
        explicit CircleLayer(Graphics& graphics);

        IDCompositionVisual* Visual() const { return visual.get(); }

        /// Shows the circle (radius in document units) around a document point at `renderScale` pixels per unit.
        void Show(PointF at, float radius, float renderScale);
        void Hide();

    private:
        void Draw();

        Graphics& graphics;
        winrt::com_ptr<IDCompositionVisual> visual;
        winrt::com_ptr<IDCompositionSurface> surface;
        float radius = 0, scale = 0;
        bool shown = false;
    };
}
