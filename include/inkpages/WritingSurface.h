#pragma once

// The writing surface: a child window the app places, showing the document's pages stacked vertically on the paper
// they are printed with, with Windows ink on them.
//   - Finger: pan with inertia, pinch zoom. Precision touchpad: the same. Mouse wheel scrolls, Ctrl+wheel zooms.
//     (DirectManipulation.) Pen and mouse buttons never pan.
//   - Pen: writes (Windows ink presenter), with the pen cursor the app chooses (a cursor resource of its exe).
//   - Lazy pages: only pages near the view hold their strokes in the presenter; the app gives and takes them (page
//     coordinates) through `load`/`store`, and can keep a page's ink (Hold).
// All methods: UI thread. Callbacks marked "ink thread" run on Windows' ink host thread.

#include <windows.h>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include <winrt/Windows.UI.Input.Inking.h>

#include "inkpages/PageLayout.h"
#include "inkpages/Paper.h"
#include "inkpages/ViewMath.h"

namespace inkpages
{
    struct PageSpec
    {
        SizeF size{ 595.28f, 841.89f }; // DIPs (A4: 210 x 297 mm)
        Paper paper;
    };

    struct SurfaceOptions
    {
        float minZoom = 0.2f, maxZoom = 6;
        float gap = 24, margin = 24;          // between pages, around the document (DIPs)
        Color background{ 0xFF, 0xE6, 0xE6, 0xE6 };
        bool mouseInks = false;               // the mouse is a pointer; true lets it write (desktop testing)
    };

    class WritingSurface
    {
    public:
        static std::unique_ptr<WritingSurface> Create(HWND parent, SurfaceOptions options = {});
        virtual ~WritingSurface() = default;

        virtual HWND Window() const = 0;

        /// The pages, top to bottom. Replaces the document (zoom stays, the view goes to the top).
        virtual void SetPages(std::vector<PageSpec> pages) = 0;
        virtual PageLayout const& Layout() const = 0;

        virtual void ZoomTo(float zoom) = 0;    // around the view's center
        virtual void FitWidth() = 0;
        virtual void ScrollToPage(int page) = 0;
        virtual View CurrentView() const = 0;   // DirectManipulation's transform (pixels)
        virtual float Scale() const = 0;        // pixels per DIP (the window's DPI / 96)
        virtual std::optional<int> PageInView() const = 0;
        /// The view moved or zoomed (UI thread; while moving too).
        std::function<void()> viewChanged;

        /// The pen: color and size (DIPs at half pressure, Windows ink's meaning) and its cursor (resource id in the
        /// app's exe, 0: Windows' default).
        virtual void SetPen(Color color, float size, UINT cursorResourceId) = 0;

        /// Keeps a page's ink in the presenter even when it is far from the view (e.g. while it has an edit).
        virtual void Hold(int page, bool hold) = 0;

        using InkStroke = winrt::Windows::UI::Input::Inking::InkStroke;
        /// Lazy pages (ink thread): a page coming near asks for its strokes, a page going far hands them over.
        std::function<std::vector<InkStroke>(int page)> load;
        std::function<void(int page, std::vector<InkStroke> strokes)> store;
        /// A stroke was written (ink thread; document coordinates; the page it belongs to).
        std::function<void(int page, InkStroke const& stroke)> strokeCollected;
    };
}
