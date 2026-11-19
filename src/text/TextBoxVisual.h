#pragma once

// The edited text box on the page (UI thread): a DirectComposition visual in the text layer (document coordinates,
// under the tools' overlay and the ink) holding a surface the RichEdit draws into at the settled zoom's resolution -
// it pans and zooms with the pages - plus the caret (a D2D host draws it itself) and a thin frame.

#include <windows.h>
#include <dcomp.h>

#include <winrt/base.h>

#include "inkpages/model/Paper.h"
#include "render/Graphics.h"
#include "text/RichEditBox.h"

namespace inkpages::detail
{
    class TextBoxVisual
    {
    public:
        TextBoxVisual(Graphics& graphics, IDCompositionVisual* layer);
        ~TextBoxVisual();
        TextBoxVisual(TextBoxVisual const&) = delete;
        TextBoxVisual& operator=(TextBoxVisual const&) = delete;

        struct Placement
        {
            PointF at;          // the text's top left (document)
            SIZE text{};        // the text's size, pixels
            float pixelsPerUnit = 1; // the settled zoom's resolution (DPI scale * zoom)
            float dpiScale = 1;
        };
        /// Draws the box: the RichEdit's text, the caret when `caretOn`, the frame.
        void Draw(RichEditBox& box, Placement const& place, Color caretColor, bool caretOn);
        /// The margin around the text (pixels) the frame lies in.
        static int Margin(float dpiScale) { return static_cast<int>(dpiScale * 3 + 0.5f); }

    private:
        Graphics& graphics;
        winrt::com_ptr<IDCompositionVisual> layer, visual;
        winrt::com_ptr<IDCompositionSurface> surface;
        SIZE surfaceSize{};
    };
}
