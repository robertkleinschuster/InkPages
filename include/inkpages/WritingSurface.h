#pragma once

// The writing surface: a child window the app places, showing the document's pages stacked vertically on the paper
// they are printed with, with Windows ink on them.
//   - Finger: pan with inertia, pinch zoom. Precision touchpad: the same. Mouse wheel scrolls, Ctrl+wheel zooms.
//     (DirectManipulation.) Pen and mouse buttons never pan.
//   - Pen: writes (Windows ink presenter) with the active tool - pen, highlighter or eraser (whole strokes: Windows
//     ink's; exact: the circle at the pen) - and the pen cursor the app chooses (a cursor resource of its exe). The
//     pen's eraser end erases with any tool. Undo/redo: one history across pages.
//   - Lazy pages: only pages near the view hold their strokes in the presenter; the app gives and takes them (page
//     coordinates) through `load`/`store`, and can keep a page's ink (Hold).
// All methods: UI thread. Callbacks marked "ink thread" run on Windows' ink host thread.

#include <windows.h>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include <winrt/Windows.UI.Input.Inking.h>

#include "inkpages/model/PageLayout.h"
#include "inkpages/model/Paper.h"
#include "inkpages/model/Pens.h"
#include "inkpages/model/ViewMath.h"

namespace inkpages
{
    struct PageSpec
    {
        SizeF size{ 595.28f, 841.89f }; // page units (the default: A4 in points, as Xournal++ stores it)
        Paper paper;
    };

    struct SurfaceOptions
    {
        float minZoom = 0.2f, maxZoom = 6;
        float gap = 24, margin = 24;          // between pages, around the document (DIPs)
        Color background{ 0xFF, 0xE6, 0xE6, 0xE6 };
        bool mouseInks = false;               // the mouse is a pointer; true lets it write (desktop testing)
    };

    /// What the pen does: the active tool with its look (the toolbar makes it from ToolChoices).
    struct ToolState
    {
        Tool tool = Tool::Pen;
        Color color{ 0xFF, 0, 0, 0 };
        float width = ToolChoices::DefaultPenWidth; // pens: width at full pressure; highlighters: drawn width
        UINT cursor = 0;                            // cursor resource in the app's exe (0: Windows' default)
        bool wholeStrokeErasing = false;            // the eraser (and the pen's eraser end) erases whole strokes
        float eraserRadius = ToolChoices::DefaultEraserRadius; // the exact eraser's circle (page units)
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

        /// The active tool (the toolbar sets it).
        virtual void SetTool(ToolState tool) = 0;

        /// One history in time order across pages: strokes written, erased (whole or exactly).
        virtual void Undo() = 0;
        virtual void Redo() = 0;
        virtual bool CanUndo() const = 0;
        virtual bool CanRedo() const = 0;
        /// The content as it is now was saved; whether it still (or again, after undoing back) is.
        virtual void MarkSaved() = 0;
        virtual bool AtSavePoint() const = 0;
        /// The history changed: enable undo/redo, mark the document as changed (UI thread).
        std::function<void()> historyChanged;

        /// Keeps a page's ink in the presenter even when it is far from the view. (Pages with undo history are kept by
        /// the surface itself.)
        virtual void Hold(int page, bool hold) = 0;

        using InkStroke = winrt::Windows::UI::Input::Inking::InkStroke;
        /// Lazy pages (ink thread): a page coming near asks for its strokes, a page going far hands them over.
        std::function<std::vector<InkStroke>(int page)> load;
        std::function<void(int page, std::vector<InkStroke> strokes)> store;
        /// A stroke was written (ink thread; document coordinates; the page it belongs to).
        std::function<void(int page, InkStroke const& stroke)> strokeCollected;
    };
}
