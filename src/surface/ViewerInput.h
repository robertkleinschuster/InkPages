#pragma once

// What makes the surface a viewer with the mouse and the keyboard, beside DirectManipulation's wheel and touch:
//   - Keys (the surface has the keyboard focus): first DirectManipulation's (ProcessInput with the keyboard focus
//     contact); a key it does not take scrolls here - arrows a line, Bild auf/ab and (Umschalt+)Leertaste a view height
//     less an overlap, Pos1/Ende (also with Strg) to the document's start and end.
//   - The middle mouse button dragged pans the pages (the content follows the mouse; Windows' move cursor meanwhile).
//   - The real mouse hovering without a button: the app's cursor for the place (`cursorAt`: a link's hand).
// UI thread.

#include <windows.h>

#include <functional>

#include "inkpages/model/PageLayout.h"
#include "inkpages/model/ViewMath.h"

namespace inkpages::detail
{
    class Viewport;

    struct ViewerInputContext
    {
        HWND window = nullptr;
        Viewport* viewport = nullptr;
        std::function<SizeF()> clientSize;                       // pixels
        std::function<float()> scale;                            // pixels per DIP
        std::function<PageLayout const&()> layout;
        std::function<void(RectF content)> zoomToRect;           // kept on the content
        std::function<PointF(POINT client)> toDocument;
        std::function<LPCWSTR(int page, PointF point)> cursorAt; // the app's (may be empty)
    };

    class ViewerInput
    {
    public:
        explicit ViewerInput(ViewerInputContext context) : c(std::move(context)) {}

        /// WM_KEYDOWN; true when the key scrolled.
        bool OnKey(WPARAM key, LPARAM flags);

        /// WM_MBUTTONDOWN / WM_MOUSEMOVE / WM_MBUTTONUP of the real mouse; true when it is the pan's.
        void PanDown(POINT client);
        bool PanMove(POINT client);
        bool PanUp();
        void CaptureLost();      // WM_CAPTURECHANGED to another window: the pan ends
        bool Panning() const { return panning; }

        /// The real mouse moved without a button over the window.
        void Hover(POINT client);
        /// Mouse messages Windows made from the pen or touch, or a button pressed: no hover cursor.
        void NotHovering();
        /// WM_MOUSELEAVE.
        void Left();
        /// WM_SETCURSOR in the client area: the pan's or the hover's cursor; false: the default.
        bool OnSetCursor();

    private:
        void Report(int page, PointF point, bool apply = true);

        ViewerInputContext c;
        bool panning = false;
        POINT panFrom{};
        View panView;
        LPCWSTR hoverCursor = nullptr; // a system cursor id; nullptr: the default
        bool tracking = false;         // TrackMouseEvent asked for WM_MOUSELEAVE
    };
}
