#include "inkpages/WritingSurface.h"

#include <cmath>
#include <format>
#include <mutex>
#include <set>

#include <winrt/Windows.Foundation.Collections.h>

#include "CircleLayer.h"
#include "Graphics.h"
#include "InkLayer.h"
#include "PaperLayer.h"
#include "Viewport.h"
#include "inkpages/Log.h"

namespace inkpages
{
    namespace
    {
        using namespace detail;

        constexpr wchar_t ClassName[] = L"InkPagesWritingSurface";
        constexpr UINT CommitMessage = WM_APP + 0x101;   // the presenter asks for a DirectComposition commit
        constexpr UINT ReadyMessage = WM_APP + 0x102;    // the presenter exists
        constexpr UINT HistoryMessage = WM_APP + 0x103;  // the undo history changed
        constexpr UINT CircleMessage = WM_APP + 0x104;   // the exact eraser's circle moved
        constexpr UINT_PTR MotionTimer = 2;
        constexpr UINT MotionIntervalMs = 120;           // lazy pages and sheets while the view moves

        // The sheets' render scale: quantized, so tiny zoom differences after a gesture do not redraw.
        float RenderScale(float scale, float zoom) { return std::round(scale * zoom * 64) / 64; }

        class Surface final : public WritingSurface
        {
        public:
            Surface(HWND parent, SurfaceOptions options);
            ~Surface() override;

            HWND Window() const override { return window; }
            void SetPages(std::vector<PageSpec> pages) override;
            PageLayout const& Layout() const override { return layout; }
            void ZoomTo(float zoom) override;
            void FitWidth() override;
            void ScrollToPage(int page) override;
            View CurrentView() const override { return viewport ? viewport->Current() : View{}; }
            float Scale() const override { return scale; }
            std::optional<int> PageInView() const override { return layout.PageInView(Visible()); }
            void SetTool(ToolState state) override;
            void Undo() override { inkLayer->Undo(); }
            void Redo() override { inkLayer->Redo(); }
            bool CanUndo() const override { return inkLayer->History().canUndo; }
            bool CanRedo() const override { return inkLayer->History().canRedo; }
            void MarkSaved() override { inkLayer->MarkSaved(); }
            bool AtSavePoint() const override { return inkLayer->History().atSavePoint; }
            void Hold(int page, bool hold) override;

            static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wp, LPARAM lp);

        private:
            LRESULT Handle(UINT message, WPARAM wp, LPARAM lp);
            SizeF ClientSize() const;
            SizeF Content() const { return { layout.DocumentSize().width * scale, layout.DocumentSize().height * scale }; }
            RectF Visible() const;
            void Resized();
            void SetScale();
            void Changed(bool settled);
            void Whenready(std::function<void()> action);

            SurfaceOptions options;
            HWND window = nullptr;
            float scale = 1;
            Graphics graphics;
            winrt::com_ptr<IDCompositionTarget> target;
            winrt::com_ptr<IDCompositionVisual> root, content, sheets, ink;
            std::unique_ptr<CircleLayer> circle;
            ToolState tool;
            std::unique_ptr<Viewport> viewport;
            std::unique_ptr<InkLayer> inkLayer;
            std::vector<std::unique_ptr<PaperLayer>> papers;
            std::vector<PageSpec> pages;
            PageLayout layout;
            std::set<int> live, held;
            float sheetScale = 0;
            bool presenterReady = false;
            std::vector<std::function<void()>> pending; // until the viewport is ready
            mutable std::mutex viewLock;
            View inkView; // a copy for the ink thread's log
        };

        Surface::Surface(HWND parent, SurfaceOptions options) : options(options)
        {
            static bool registered = [] {
                WNDCLASSEXW wc{ sizeof(wc) };
                wc.lpfnWndProc = WindowProc;
                wc.hInstance = GetModuleHandleW(nullptr);
                wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
                wc.lpszClassName = ClassName;
                winrt::check_bool(RegisterClassExW(&wc));
                return true;
            }();
            (void)registered;
            window = CreateWindowExW(0, ClassName, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 0, 0, 100, 100, parent,
                nullptr, GetModuleHandleW(nullptr), this);
            winrt::check_pointer(window);
            scale = GetDpiForWindow(window) / 96.f;

            auto& dc = graphics.dcomp;
            winrt::check_hresult(dc->CreateTargetForHwnd(window, TRUE, target.put()));
            winrt::check_hresult(dc->CreateVisual(root.put()));
            winrt::check_hresult(dc->CreateVisual(content.put()));
            winrt::check_hresult(dc->CreateVisual(sheets.put()));
            winrt::check_hresult(dc->CreateVisual(ink.put()));
            // AddVisual(v, FALSE, nullptr) puts v above its siblings: the ink above the sheets (it must be on top to
            // get the pen - measured: under an opaque sheet it got no input).
            winrt::check_hresult(root->AddVisual(content.get(), FALSE, nullptr));
            circle = std::make_unique<CircleLayer>(graphics);
            winrt::check_hresult(content->AddVisual(sheets.get(), FALSE, nullptr));
            winrt::check_hresult(content->AddVisual(circle->Visual(), FALSE, nullptr)); // on the sheets, below the ink
            winrt::check_hresult(content->AddVisual(ink.get(), FALSE, nullptr));
            winrt::check_hresult(target->SetRoot(root.get()));
            SetScale();

            viewport = std::make_unique<Viewport>(window, dc.get(), root.get(), content.get(), options.minZoom, options.maxZoom);
            viewport->onChanged = [this](View view) {
                {
                    std::scoped_lock guard(viewLock);
                    inkView = view;
                }
                if (!viewport->Moving()) Changed(true);
                if (viewChanged) viewChanged();
            };
            viewport->onStatus = [this](bool settled) {
                if (viewport->Moving()) SetTimer(window, MotionTimer, MotionIntervalMs, nullptr);
                if (settled)
                {
                    KillTimer(window, MotionTimer);
                    Changed(true);
                    auto run = std::move(pending);
                    pending.clear();
                    for (auto& action : run) action();
                }
            };

            inkLayer = std::make_unique<InkLayer>(window, InkMessages{ CommitMessage, ReadyMessage, HistoryMessage, CircleMessage },
                ink.get(), options.mouseInks);
            inkLayer->load = [this](int page) { return load ? load(page) : std::vector<InkStroke>{}; };
            inkLayer->store = [this](int page, std::vector<InkStroke> strokes) { if (store) store(page, std::move(strokes)); };
            inkLayer->collected = [this](int page, InkStroke const& stroke) {
                View view;
                {
                    std::scoped_lock guard(viewLock);
                    view = inkView;
                }
                auto points = stroke.GetInkPoints();
                auto attributes = stroke.DrawingAttributes();
                auto c = attributes.Color();
                std::string text = std::format("ink: stroke on page {} ({} points, #{:02X}{:02X}{:02X}{}, size {:.2f})", page,
                    points.Size(), c.R, c.G, c.B, attributes.DrawAsHighlighter() ? " highlighter" : "", attributes.Size().Width);
                for (uint32_t i : { 0u, points.Size() - 1 })
                {
                    auto p = points.GetAt(i).Position();
                    PointF px = DocumentToViewport({ p.X, p.Y }, view, scale);
                    text += std::format("; {} doc {:.1f},{:.1f} -> client {:.1f},{:.1f} DIP", i == 0 ? "first" : "last",
                        p.X, p.Y, px.x / scale, px.y / scale);
                }
                Log(text);
                if (strokeCollected) strokeCollected(page, stroke);
            };
            Resized();
            winrt::check_hresult(dc->Commit());
            Log(std::format("surface: created (DPI scale {:.2f})", scale));
        }

        Surface::~Surface()
        {
            try
            {
                inkLayer.reset();
                viewport.reset();
                papers.clear();
                target = nullptr;
            }
            catch (...)
            {
                LogException("surface shutdown");
            }
            if (window)
            {
                SetWindowLongPtrW(window, GWLP_USERDATA, 0);
                DestroyWindow(window);
            }
        }

        LRESULT CALLBACK Surface::WindowProc(HWND window, UINT message, WPARAM wp, LPARAM lp)
        {
            if (message == WM_NCCREATE)
            {
                auto self = static_cast<Surface*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
                SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            }
            auto self = reinterpret_cast<Surface*>(GetWindowLongPtrW(window, GWLP_USERDATA));
            if (self && self->window)
            {
                try
                {
                    return self->Handle(message, wp, lp);
                }
                catch (...)
                {
                    LogException(std::format("surface: message 0x{:X}", message));
                    return 0;
                }
            }
            return DefWindowProcW(window, message, wp, lp);
        }

        LRESULT Surface::Handle(UINT message, WPARAM wp, LPARAM lp)
        {
            switch (message)
            {
            case WM_POINTERDOWN:
            case DM_POINTERHITTEST:
                // Touch and touchpad contacts go to DirectManipulation; pen and mouse never pan.
                if (viewport->OnPointer(wp)) return 0;
                break;
            case WM_MOUSEWHEEL:
            case WM_MOUSEHWHEEL:
            {
                MSG msg{ window, message, wp, lp };
                if (viewport->OnWheel(msg)) return 0;
                break;
            }
            case WM_SIZE:
                Resized();
                return 0;
            case WM_DPICHANGED_AFTERPARENT:
                SetScale();
                Resized();
                return 0;
            case CommitMessage:
                winrt::check_hresult(graphics.dcomp->Commit());
                return 0;
            case ReadyMessage:
                presenterReady = true;
                winrt::check_hresult(graphics.dcomp->Commit());
                Changed(true);
                return 0;
            case HistoryMessage:
                Changed(!viewport->Moving()); // pages with history keep their ink
                if (historyChanged) historyChanged();
                return 0;
            case CircleMessage:
                if (lp) circle->Show(InkLayer::UnpackPoint(wp), tool.eraserRadius, RenderScale(scale, CurrentView().zoom));
                else circle->Hide();
                winrt::check_hresult(graphics.dcomp->Commit());
                return 0;
            case WM_TIMER:
                if (wp == MotionTimer)
                {
                    Changed(false);
                }
                return 0;
            case WM_ERASEBKGND:
            {
                RECT rc;
                GetClientRect(window, &rc);
                Color c = options.background;
                HBRUSH brush = CreateSolidBrush(RGB(c.r, c.g, c.b));
                FillRect(reinterpret_cast<HDC>(wp), &rc, brush);
                DeleteObject(brush);
                return 1;
            }
            }
            return DefWindowProcW(window, message, wp, lp);
        }

        SizeF Surface::ClientSize() const
        {
            RECT rc;
            GetClientRect(window, &rc);
            return { static_cast<float>(rc.right), static_cast<float>(rc.bottom) };
        }

        RectF Surface::Visible() const
        {
            return VisibleDocument(ClientSize(), CurrentView(), scale);
        }

        void Surface::SetScale()
        {
            scale = GetDpiForWindow(window) / 96.f;
            D2D1_MATRIX_3X2_F m{ scale, 0, 0, scale, 0, 0 };
            winrt::check_hresult(content->SetTransform(m));
            if (viewport)
            {
                SizeF doc = layout.DocumentSize();
                viewport->SetContentSize({ static_cast<LONG>(doc.width * scale), static_cast<LONG>(doc.height * scale) });
            }
        }

        void Surface::Resized()
        {
            RECT rc;
            GetClientRect(window, &rc);
            if (viewport) viewport->SetViewportRect(rc);
            winrt::com_ptr<IDCompositionRectangleClip> clip;
            winrt::check_hresult(graphics.dcomp->CreateRectangleClip(clip.put()));
            clip->SetRight(static_cast<float>(rc.right));
            clip->SetBottom(static_cast<float>(rc.bottom));
            winrt::check_hresult(root->SetClip(clip.get()));
            Changed(true);
        }

        void Surface::SetPages(std::vector<PageSpec> specs)
        {
            inkLayer->SetLivePages({}); // the old document's ink goes to `store`
            live.clear();
            held.clear();
            pages = std::move(specs);
            std::vector<SizeF> sizes;
            for (PageSpec const& p : pages) sizes.push_back(p.size);
            layout = PageLayout(sizes, options.gap, options.margin);

            papers.clear();
            winrt::check_hresult(sheets->RemoveAllVisuals());
            for (int i = 0; i < layout.Count(); ++i)
            {
                papers.push_back(std::make_unique<PaperLayer>(graphics, layout.Page(i), pages[i].paper));
                winrt::check_hresult(sheets->AddVisual(papers.back()->Visual(), FALSE, nullptr));
            }
            sheetScale = 0;
            SetScale();
            inkLayer->SetLayout(layout);
            Log(std::format("surface: {} pages, document {:.0f}x{:.0f} DIP", layout.Count(), layout.DocumentSize().width,
                layout.DocumentSize().height));
            Changed(true);
        }

        void Surface::Changed(bool settled)
        {
            if (layout.Count() == 0) return;
            RectF visible = Visible();
            float h = visible.Height();
            // Sheets: drawn at the settled zoom's resolution, near the view.
            if (settled) sheetScale = RenderScale(scale, CurrentView().zoom);
            if (sheetScale <= 0) sheetScale = RenderScale(scale, 1);
            RectF needed = visible.Inflated(visible.Width() / 2, h / 2);
            bool drew = false;
            for (int i = 0; i < layout.Count(); ++i)
            {
                if (layout.Page(i).Intersects(needed)) drew |= papers[i]->Update(needed, sheetScale);
                else if (!layout.Page(i).Intersects(visible.Inflated(0, 3 * h))) papers[i]->Discard();
            }
            // Ink: pages near the view.
            if (presenterReady)
            {
                std::set<int> keep = held;
                for (int page : inkLayer->History().pages) keep.insert(page);
                std::set<int> next = LivePages(layout, visible, live, keep);
                if (next != live)
                {
                    live = next;
                    inkLayer->SetLivePages(live);
                }
            }
            if (drew) winrt::check_hresult(graphics.dcomp->Commit());
        }

        void Surface::Whenready(std::function<void()> action)
        {
            if (viewport->Ready()) action();
            else pending.push_back(std::move(action));
        }

        void Surface::ZoomTo(float zoom)
        {
            Whenready([this, zoom] {
                SizeF size = ClientSize();
                float z = ClampZoom(zoom, options.minZoom, options.maxZoom);
                viewport->ZoomToRect(KeepOnContent(ZoomRect(size, CurrentView(), z, { size.width / 2, size.height / 2 }), Content()), false);
            });
        }

        void Surface::FitWidth()
        {
            Whenready([this] {
                SizeF size = ClientSize();
                View view = CurrentView();
                float z = ClampZoom(FitWidthZoom(layout.DocumentSize().width, size.width, scale), options.minZoom, options.maxZoom);
                float top = -view.dy / view.zoom;
                viewport->ZoomToRect(KeepOnContent({ 0, top, size.width / z, top + size.height / z }, Content()), false);
            });
        }

        void Surface::ScrollToPage(int page)
        {
            if (page < 0 || page >= layout.Count()) return;
            Whenready([this, page] {
                float top = std::max(0.f, layout.Page(page).top - options.gap / 2);
                viewport->ZoomToRect(KeepOnContent(ScrollRect(ClientSize(), CurrentView(), scale, top), Content()), false);
            });
        }

        void Surface::SetTool(ToolState state)
        {
            tool = state;
            inkLayer->SetTool(state);
            if (state.tool != Tool::Eraser || state.wholeStrokeErasing)
            {
                circle->Hide();
                winrt::check_hresult(graphics.dcomp->Commit());
            }
        }

        void Surface::Hold(int page, bool hold)
        {
            if (hold) held.insert(page);
            else held.erase(page);
            Changed(!viewport->Moving());
        }
    }

    std::unique_ptr<WritingSurface> WritingSurface::Create(HWND parent, SurfaceOptions options)
    {
        return std::make_unique<Surface>(parent, options);
    }
}
