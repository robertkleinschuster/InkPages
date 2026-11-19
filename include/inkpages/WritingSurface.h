#pragma once

// The writing surface: a child window the app places, showing the document's pages stacked vertically on their paper,
// with Windows ink on them and the tools that work on it.
//   - Finger and precision touchpad: pan with inertia, pinch zoom; mouse wheel scrolls, Ctrl+wheel zooms
//     (DirectManipulation). Touch never draws; pen and mouse buttons never pan.
//   - Pen: the active tool (ToolState): pen, highlighter, eraser (whole strokes or exactly the circle), selection
//     (lasso or rectangle; the barrel button lassos with any tool; the pen over the selection moves and scales it),
//     shapes (and recognition of drawn shapes). The pen's eraser end erases with any tool. Windows' ruler/protractor.
//   - One undo history across pages. Strokes belong to pages and never cross one (cut at the page's edge).
//   - The app's content: page layers below the ink to draw on (PDF pages, pictures), and its pictures in the
//     selection (PictureHost). Lazy pages: only pages near the view hold their strokes in the ink presenter; the app
//     gives and takes them through `load`/`store`.
// All methods and callbacks on the UI thread unless marked "ink thread".

#include <windows.h>
#include <d2d1_1.h>
#include <dcomp.h>

#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <vector>

#include <winrt/Windows.UI.Input.Inking.h>

#include "inkpages/Pictures.h"
#include "inkpages/model/PageLayout.h"
#include "inkpages/model/Paper.h"
#include "inkpages/model/Pens.h"
#include "inkpages/model/ViewMath.h"

namespace inkpages
{
    using InkStroke = winrt::Windows::UI::Input::Inking::InkStroke;

    struct PageSpec
    {
        SizeF size{ 595.28f, 841.89f }; // page units (the default: A4 in points, as Xournal++ stores it)
        Paper paper;
    };

    struct SurfaceOptions
    {
        float minZoom = 0.2f, maxZoom = 6;
        float gap = 24, margin = 24;          // between pages, around the document (page units)
        Color background{ 0xFF, 0xE6, 0xE6, 0xE6 };
        bool mouseInks = false;               // the mouse is a pointer; true lets it write (desktop testing)
    };

    /// What the pen does (the toolbar makes it from ToolChoices).
    struct ToolState
    {
        Tool tool = Tool::Pen;
        Color color{ 0xFF, 0, 0, 0 };
        float width = ToolChoices::DefaultPenWidth; // pens and shapes: width at full pressure; highlighters: drawn width
        UINT cursor = 0;                            // cursor resource in the app's exe (0: Windows' default)
        bool wholeStrokeErasing = false;            // the eraser (and the pen's eraser end) erases whole strokes
        float eraserRadius = ToolChoices::DefaultEraserRadius; // the exact eraser's circle (page units)
        SelectMode selectMode = SelectMode::Lasso;
        ShapeKind shape = ShapeKind::Line;
        bool recognizeShapes = false;
    };

    /// A stroke as the app keeps it: page coordinates, the app's key (0: new ink - written, an erased piece, cut at a
    /// page's edge, duplicated or pasted), and the transform a move or scale applied since it was loaded with that key
    /// (page coordinates: from the points as loaded on their page to the points now on this page; identity when
    /// unchanged; the pen width is never scaled). Undo/redo that brings a stroke back brings its key back.
    struct KeyedStroke
    {
        InkStroke stroke{ nullptr };
        uint64_t key = 0;
        Affine transform;
    };

    /// A selection as the app's clipboard keeps it: strokes relative to the selection's top left, the app's pictures.
    struct Clip
    {
        std::vector<InkStroke> strokes;
        std::vector<PictureId> pictures;
        RectF bounds; // where it was (document)
    };

    class WritingSurface
    {
    public:
        static std::unique_ptr<WritingSurface> Create(HWND parent, SurfaceOptions options = {});
        virtual ~WritingSurface() = default;

        virtual HWND Window() const = 0;

        // ---- pages and view
        /// The pages, top to bottom. Replaces the document (the history is cleared).
        virtual void SetPages(std::vector<PageSpec> pages) = 0;
        virtual PageLayout const& Layout() const = 0;
        virtual void ZoomTo(float zoom) = 0;    // around the view's center
        virtual void FitWidth() = 0;
        virtual void ScrollToPage(int page) = 0;
        virtual View CurrentView() const = 0;   // DirectManipulation's transform (pixels)
        virtual float Scale() const = 0;        // pixels per DIP (the window's DPI / 96)
        virtual std::optional<int> PageInView() const = 0;
        std::function<void()> viewChanged;      // moved or zoomed (also while moving)

        // ---- the app's content below the ink
        /// The underlay of a page: what lies under the ink (background and ruling, a rendered PDF page, the app's
        /// pictures and texts, strokes Windows ink cannot draw). The surface calls it to draw into `dc` in page
        /// coordinates (the transform maps them to pixels at the settled zoom), over the page's Paper (an app that draws
        /// its own background gives the pages PaperKind::Plain), for the part `area` (page coordinates) of a page near
        /// the view; again when the zoom settles or after InvalidatePage. Keep it fast: it runs on the UI thread.
        std::function<void(int page, ID2D1DeviceContext* dc, RectF area)> drawUnderlay;
        /// The page's underlay changed (a PDF render arrived, a picture moved): drawn anew.
        virtual void InvalidatePage(int page) = 0;
        /// A visual for the app's content of a page (PDF page, pictures): page units, origin at the page's top left,
        /// above the paper, below the ink. Draw with the surface's device; sharpness: `RenderScale`.
        virtual IDCompositionVisual* PageLayer(int page) = 0;
        virtual IDCompositionDevice* CompositionDevice() = 0;
        /// Pixels per page unit at the settled zoom; `renderScaleChanged` when it changes (draw anew then).
        virtual float RenderScale() const = 0;
        std::function<void()> renderScaleChanged;
        /// Commit after drawing into page layers.
        virtual void Commit() = 0;
        virtual void SetPictureHost(PictureHost* host) = 0;

        // ---- tools
        virtual void SetTool(ToolState tool) = 0;
        virtual void SetRuler(bool shown) = 0;       // Windows' ruler (InkPresenterRuler)
        virtual void SetProtractor(bool shown) = 0;  // Windows' protractor (InkPresenterProtractor)

        // ---- selection
        virtual bool HasSelection() const = 0;
        std::function<void()> selectionChanged;
        virtual void Cut() = 0;
        virtual void Copy() = 0;
        virtual void Paste() = 0;                // at the view's center, then selected
        virtual void Duplicate() = 0;
        virtual void DeleteSelection() = 0;
        virtual void SelectAll() = 0;            // the page in view
        virtual void ClearSelection() = 0;
        /// The app's own clipboard format (unset: Windows ink's clipboard). `pasteFromClipboard` gets the document
        /// point to paste at; the app adds the clip's pictures there itself and returns their ids with the strokes.
        std::function<void(Clip const&)> copyToClipboard;
        std::function<std::optional<Clip>(PointF at)> pasteFromClipboard;

        // ---- history
        virtual void Undo() = 0;
        virtual void Redo() = 0;
        virtual bool CanUndo() const = 0;
        virtual bool CanRedo() const = 0;
        virtual void MarkSaved() = 0;
        virtual bool AtSavePoint() const = 0;
        std::function<void()> historyChanged;
        /// The pages an edit (or its undo/redo) changed: the app's data of them is out of date (StrokesOf).
        std::function<void(std::set<int> const& pages)> contentChanged;

        // ---- strokes (the app persists them; InkStroke keeps points with pressure, tilt and time, and its attributes;
        //      the key says which of the app's strokes it is, see KeyedStroke)
        /// The strokes of a page now, in page coordinates (copies; e.g. to save). Waits for the ink thread. A page whose
        /// ink is not in the presenter (lazy) gives nothing: the app holds its strokes since `store`.
        virtual std::vector<KeyedStroke> StrokesOf(int page) = 0;
        /// Keeps a page's ink in the presenter even when it is far from the view (pages with history are kept anyway).
        virtual void Hold(int page, bool hold) = 0;
        /// Lazy pages (ink thread): a page coming near asks for its strokes, a page going far hands them over (page
        /// coordinates).
        std::function<std::vector<KeyedStroke>(int page)> load;
        std::function<void(int page, std::vector<KeyedStroke> strokes)> store;
    };
}
