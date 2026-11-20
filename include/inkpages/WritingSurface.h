#pragma once

// The writing surface: a child window the app places, showing the document's pages stacked vertically on their paper,
// with Windows ink on them and the tools that work on it.
//   - Finger and precision touchpad: pan with inertia, pinch zoom; mouse wheel scrolls, Ctrl+wheel zooms
//     (DirectManipulation). Touch never draws.
//   - A viewer's mouse and keys: the middle mouse button dragged pans; with the keyboard focus (the surface takes it on
//     a mouse press) DirectManipulation's keys, else arrows scroll a line, Bild auf/ab and (Umschalt+)Leertaste a
//     screen, Pos1/Ende go to the start/end; the app's cursor over its places (`cursorAt`: links).
//   - Mouse (and touchpad clicks): never writes. A left drag moves or scales the selection, selects with the selection
//     tool, or pans in viewer mode. A right drag lassos with any tool. A right click opens the app's context menu.
//   - Text tool: a pen tap or mouse click writes a text there (or edits the text there), the keyboard types (TextHost,
//     Texts.h); a pen drag writes nothing, a mouse drag outside the editor behaves as in the viewer.
//   - Taps: a mouse click or finger tap that no selection gesture took is the app's (`tapped`: links).
//   - The app's mouse gestures: a drag or double/triple click where the app claims the mouse (`mouseClaims`) is the
//     app's (`mouseGesture`) in viewer mode, e.g. selecting PDF text through ink.
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
#include "inkpages/Texts.h"
#include "inkpages/model/PageLayout.h"
#include "inkpages/model/PageMap.h"
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
        TextStyle text;                             // the Text tool's font, size and color
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

    enum class PointerKind { Mouse, Touch };

    enum class MousePhase { Begin, Move, End, Cancel };

    /// A mouse gesture the app claimed (WritingSurface::mouseGesture).
    struct MouseGesture
    {
        MousePhase phase = MousePhase::Begin;
        int page = 0;   // the page of the press; every point of the gesture is in its coordinates (also beyond its edges)
        PointF point;   // Begin: the press point; Move/End: the mouse now; Cancel: the last point
        int clicks = 1; // 1: a drag (begins once the mouse left the tap distance); 2, 3: a double or triple click (begins
                        // at the press - a drag after it goes on as Move)
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
        /// Page operations that keep the history: edits of other pages follow them; edits of a removed page are dropped
        /// (removing a page cannot be undone - ask first). Strokes, stroke keys, lazy pages and page layers follow too;
        /// the app changes its own pages the same way (indices of later pages shift): its UI-side data now, the strokes it
        /// keeps for `store`d pages in `pagesRemapped` (ink thread, in order with load/store).
        virtual void InsertPages(int index, std::vector<PageSpec> pages) = 0;
        virtual void RemovePage(int index) = 0;
        virtual void MovePage(int from, int to) = 0;
        virtual PageLayout const& Layout() const = 0;
        virtual void ZoomTo(float zoom) = 0;    // around the view's center
        virtual void FitWidth() = 0;
        /// The page in view whole (half a gap around it), centered.
        virtual void FitPage() = 0;
        /// Where the view is: the page and point at its top left, and the zoom (none without pages).
        virtual std::optional<ViewPlace> CurrentPlace() const = 0;
        /// Shows `place` (as CurrentPlace gave it): its zoom (clamped), its point at the view's top left, kept on the
        /// content - Back/Forward, a document's last position.
        virtual void GoTo(ViewPlace place) = 0;
        virtual void ScrollToPage(int page) = 0;
        /// Scrolls so that `point` (page units on `page`) is at the view's top left, 16 DIPs in; the zoom stays, the
        /// view stays on the content (outline entries, links).
        virtual void ScrollTo(int page, PointF point) = 0;
        /// Scrolls only when `area` (page units on `page`) is not entirely in view, as little as needed, keeping 16 DIPs
        /// around it (stepping through search hits). The zoom stays.
        virtual void RevealRect(int page, RectF area) = 0;
        virtual View CurrentView() const = 0;   // DirectManipulation's transform (pixels)
        virtual float Scale() const = 0;        // pixels per DIP (the window's DPI / 96)
        virtual std::optional<int> PageInView() const = 0;
        virtual RectF Visible() const = 0;       // document coordinates
        /// Outlines the page in view in the system's accent color (2 page units wide, 3 away from the page, rounded) - an
        /// app turns it on for documents with more than one page. Off by default.
        virtual void MarkCurrentPage(bool mark) = 0;
        std::function<void()> viewChanged;      // moved or zoomed (also while moving)
        /// A mouse click or finger tap on a page (under 300 ms, moved under 3 page units, one finger, not a pan) that no
        /// selection gesture took (a click beside the selection clears it, a click on a picture selects it). `point`:
        /// page coordinates of the press. For the app's links; the pen never taps here.
        std::function<void(int page, PointF point, PointerKind kind)> tapped;
        /// The real mouse moves over the surface without a button pressed: `point` in page coordinates of `page` (page -1:
        /// beside the pages, the pen or touch moved, or the mouse left the window). Returns the system cursor to show there
        /// (e.g. IDC_HAND over a link) or nullptr for the default. Called for every move: keep it fast.
        std::function<LPCWSTR(int page, PointF point)> cursorAt;
        /// The app's own mouse gestures on its content (e.g. selecting PDF text through ink): asked at a left press
        /// in viewer mode outside the selection, pictures and the open text editor.
        /// `point`: page coordinates. A claimed gesture is the app's alone (`mouseGesture`, the mouse captured): no
        /// lasso, no tap. A single click stays the library's (picture, Text tool, `tapped`) - the first click of a
        /// double click too.
        std::function<bool(int page, PointF point)> mouseClaims;
        std::function<void(MouseGesture const& gesture)> mouseGesture;
        /// A rectangle selected no objects. Gives the clipped rectangle in the press page's coordinates.
        std::function<void(int page, RectF area)> emptyRectangle;
        std::function<void()> mousePannedWithWriteTool;
        std::function<void()> firstPenStroke;
        /// A finger's press-and-hold over a page in viewer mode (page coordinates and menu screen position).
        std::function<void(int page, PointF point, POINT screen)> touchHeld;

        // ---- the app's content below the ink
        /// The underlay of a page: what lies under the ink (background and ruling, a rendered PDF page, the app's
        /// pictures and texts, strokes Windows ink cannot draw). The surface calls it to draw into `dc` in page
        /// coordinates (the transform maps them to pixels at the settled zoom), over the page's Paper (an app that draws
        /// its own background gives the pages PaperKind::Plain), for the part `area` (page coordinates) of a page near
        /// the view; again when the zoom settles or after InvalidatePage. Keep it fast: it runs on the UI thread.
        std::function<void(int page, ID2D1DeviceContext* dc, RectF area)> drawUnderlay;
        /// The page's underlay changed (a PDF render arrived, a picture moved): drawn anew.
        virtual void InvalidatePage(int page) = 0;
        virtual void InvalidatePages(std::set<int> const& pages)
        {
            for (int page : pages) InvalidatePage(page);
        }
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
        /// The app's texts (Texts.h); without a host the Text tool does nothing.
        virtual void SetTextHost(TextHost* host) = 0;
        /// A text is being edited: the surface's window has the keyboard focus (an app's own key shortcuts should leave
        /// typing alone meanwhile).
        virtual bool EditingText() const = 0;
        /// Ends editing (the result goes to the app and the history); e.g. before saving. Returns once the history has
        /// the edit: AtSavePoint/CanUndo count the text right after the call.
        virtual void EndTextEditing() = 0;
        std::function<void(bool editing)> textEditingChanged;

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
        virtual void PasteAt(PointF document) = 0;
        virtual void Duplicate() = 0;
        virtual void DeleteSelection() = 0;
        virtual void SelectAll() = 0;            // the page in view
        virtual void ClearSelection() = 0;
        /// The app's own clipboard format (unset: Windows ink's clipboard). `pasteFromClipboard` gets the document
        /// point to paste at; the app adds the clip's pictures there itself and returns their ids with the strokes; nothing:
        /// Windows ink's clipboard is pasted (ink of other apps). Copy calls `copyToClipboard` before it returns, while
        /// the app's pictures are still on their pages (Cut deletes the selection right after - only when it returned
        /// true: false = nothing copied, the selection stays).
        std::function<bool(Clip const&)> copyToClipboard;
        std::function<std::optional<Clip>(PointF at)> pasteFromClipboard;
        std::function<bool(PointF document)> deferPaste; // the app is still preparing its clipboard data

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
        /// The strokes of a page now, in page coordinates (copies; e.g. to save). Waits for the ink thread (up to 3 s).
        /// None: the page's ink is not in the presenter (lazy, or not loaded yet) - the app's own strokes of it are
        /// current (those it gave to `load` or got from `store`). Throws winrt::hresult_error when the ink thread does
        /// not answer in time or fails (never save a page from a failed call).
        virtual std::optional<std::vector<KeyedStroke>> StrokesOf(int page) = 0;
        /// Keeps a page's ink in the presenter even when it is far from the view (pages with history are kept anyway).
        virtual void Hold(int page, bool hold) = 0;
        /// Lazy pages (ink thread): a page coming near asks for its strokes, a page going far hands them over (page
        /// coordinates).
        std::function<std::vector<KeyedStroke>(int page)> load;
        std::function<void(int page, std::vector<KeyedStroke> strokes)> store;
        /// Ink thread, in order with `load`/`store`: InsertPages/RemovePage/MovePage reached the ink thread - every
        /// `load`/`store` before this call used the old page indices, every one after it uses the new ones. Remap the
        /// strokes you keep for `store`d pages here (`map(oldPage)` = new index, -1: removed); the app's UI-side data
        /// is remapped on the UI thread when it calls the page operation.
        std::function<void(PageMap const& map)> pagesRemapped;
    };
}
