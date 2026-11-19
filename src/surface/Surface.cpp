#include "surface/Surface.h"

#include <cmath>
#include <format>

#include <winrt/Windows.Foundation.Collections.h>

#include "inkpages/Log.h"

namespace inkpages
{
    std::unique_ptr<WritingSurface> WritingSurface::Create(HWND parent, SurfaceOptions options)
    {
        return std::make_unique<detail::Surface>(parent, options);
    }
}

namespace inkpages::detail
{
    namespace
    {
        constexpr wchar_t ClassName[] = L"InkPagesWritingSurface";
        constexpr UINT UiMessage = WM_APP + 0x101;   // UiThread's work
        constexpr UINT_PTR MotionTimer = 2;
        constexpr UINT MotionIntervalMs = 120;       // lazy pages and sheets while the view moves

        // The sheets' render scale: quantized, so tiny zoom differences after a gesture do not redraw.
        float Quantized(float scale, float zoom) { return std::round(scale * zoom * 64) / 64; }
    }

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
        ui = std::make_unique<UiThread>(window, UiMessage);
        composition = std::make_unique<Composition>(window);
        SetScale();

        viewport = std::make_unique<Viewport>(window, composition->Devices().dcomp.get(), composition->Root(),
            composition->Content(), options.minZoom, options.maxZoom);
        viewport->onChanged = [this](View view) {
            {
                std::scoped_lock guard(viewLock);
                inkView = view;
            }
            if (!viewport->Moving()) Changed(true);
            selection->Zoomed();
            if (viewChanged) viewChanged();
        };
        viewport->onStatus = [this](bool settled) {
            if (viewport->Moving()) SetTimer(window, MotionTimer, MotionIntervalMs, nullptr);
            if (!settled) return;
            KillTimer(window, MotionTimer);
            Changed(true);
            auto run = std::move(pending);
            pending.clear();
            for (auto& action : run) action();
        };

        overlay.changed = [this] {
            if (!overlayPending.exchange(true)) ui->Post([this] { DrawOverlay(); }); // one drawing per batch of changes
        };

        selection = std::make_unique<SelectionController>(SelectionContext{ window, viewport.get(), &host, ui.get(),
            [this] { return ink.get(); }, [this] { return tools.get(); }, [this] { return pictureHost; },
            [this] { return scale; }, [this]() -> PageLayout const& { return layout; },
            [this] { if (selectionChanged) selectionChanged(); }, this });

        ink = std::make_unique<InkLayer>(host, composition->Ink(), options.mouseInks,
            [this] { OnPresenterReady(); }, [this] { ui->Post([this] { composition->Commit(); }); });
        Resized();
        composition->Commit();
        Log(std::format("surface: created (DPI scale {:.2f})", scale));
    }

    Surface::~Surface()
    {
        try
        {
            host.Run("tools shutdown", [this] {
                tools.reset();
                pageStore.reset();
            });
            ink.reset();
            selection.reset();
            viewport.reset();
            composition.reset();
        }
        catch (...)
        {
            LogException("surface shutdown");
        }
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        DestroyWindow(window);
    }

    // Ink thread, once: the tools and the lazy pages need the presenter.
    void Surface::OnPresenterReady()
    {
        pageStore = std::make_unique<PageStore>(*ink);
        pageStore->load = [this](int page) { return load ? load(page) : std::vector<KeyedStroke>{}; };
        pageStore->store = [this](int page, std::vector<KeyedStroke> strokes) { if (store) store(page, std::move(strokes)); };
        tools = std::make_unique<ToolInput>(*ink, overlay);
        tools->selected = [this](StrokeSelection const& made, std::vector<PointF> const& polygon) {
            ui->Post([this, made, polygon] { selection->Selected(made, polygon); });
        };
        tools->cleared = [this] { ui->Post([this] { selection->Cleared(); }); };
        tools->penTaken = [](bool) {};
        ink->Edits().changed = [this](HistoryState state) {
            ui->Post([this, state] {
                Changed(!viewport->Moving()); // pages with history keep their ink
                if (historyChanged) historyChanged();
                if (contentChanged && !state.changed.empty()) contentChanged(state.changed);
            });
        };
        ink->collected = [this, previous = ink->collected](std::vector<Placed> const& placed) {
            if (previous) previous(placed);
            View view;
            {
                std::scoped_lock guard(viewLock);
                view = inkView;
            }
            for (Placed const& p : placed)
            {
                auto points = p.stroke.GetInkPoints();
                auto c = p.stroke.DrawingAttributes().Color();
                std::string text = std::format("ink: stroke on page {} ({} points, #{:02X}{:02X}{:02X}{})", p.page, points.Size(),
                    c.R, c.G, c.B, p.stroke.DrawingAttributes().DrawAsHighlighter() ? " highlighter" : "");
                for (uint32_t i : { 0u, points.Size() - 1 })
                {
                    auto at = points.GetAt(i).Position();
                    PointF px = DocumentToViewport({ at.X, at.Y }, view, scale);
                    text += std::format("; {} doc {:.1f},{:.1f} -> client {:.1f},{:.1f} DIP", i == 0 ? "first" : "last",
                        at.X, at.Y, px.x / scale, px.y / scale);
                }
                Log(text);
            }
        };
        ui->Post([this] {
            presenterReady = true;
            Changed(true);
        });
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
        composition->SetScale(scale);
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
        composition->SetClip({ rc.right, rc.bottom });
        Changed(true);
    }

    void Surface::SetPages(std::vector<PageSpec> specs)
    {
        pages = std::move(specs);
        std::vector<SizeF> sizes;
        std::vector<Paper> papers;
        for (PageSpec const& p : pages)
        {
            sizes.push_back(p.size);
            papers.push_back(p.paper);
        }
        layout = PageLayout(sizes, options.gap, options.margin);
        selection->Clear();
        live.clear();
        held.clear();
        host.Post("pages", [this, layout = layout] {
            if (pageStore) pageStore->ReleaseAll(); // the old document's ink goes to `store`
            ink->Edits().Clear();
            ink->SetLayout(layout);
        });
        composition->SetPages(layout, papers, [this](int page, ID2D1DeviceContext* dc, RectF area) {
            if (drawUnderlay) drawUnderlay(page, dc, area);
        });
        sheetScale = 0;
        SetScale();
        Log(std::format("surface: {} pages, document {:.0f}x{:.0f}", layout.Count(), layout.DocumentSize().width,
            layout.DocumentSize().height));
        Changed(true);
    }

    void Surface::Changed(bool settled)
    {
        if (layout.Count() == 0) return;
        RectF visible = Visible();
        float before = sheetScale;
        if (settled || sheetScale <= 0) sheetScale = Quantized(scale, CurrentView().zoom);
        composition->UpdateSheets(visible, sheetScale);
        if (sheetScale != before)
        {
            DrawOverlay();
            if (renderScaleChanged) renderScaleChanged();
        }
        if (presenterReady)
        {
            std::set<int> keep = held;
            for (int page : ink->Edits().State().pages) keep.insert(page);
            std::set<int> next = LivePages(layout, visible, live, keep);
            if (next != live)
            {
                live = next;
                host.Post("live pages", [this, next] { if (pageStore) pageStore->SetLive(next); });
            }
        }
        composition->Commit();
    }

    void Surface::DrawOverlay()
    {
        overlayPending = false;
        composition->Overlay().Draw(overlay.Get(), sheetScale > 0 ? sheetScale : scale);
        composition->Commit();
    }

    void Surface::WhenReady(std::function<void()> action)
    {
        if (viewport->Ready()) action();
        else pending.push_back(std::move(action));
    }

    void Surface::ZoomToRect(RectF content)
    {
        SizeF doc = layout.DocumentSize();
        viewport->ZoomToRect(KeepOnContent(content, { doc.width * scale, doc.height * scale }), false);
    }

    void Surface::ZoomTo(float zoom)
    {
        WhenReady([this, zoom] {
            SizeF size = ClientSize();
            float z = ClampZoom(zoom, options.minZoom, options.maxZoom);
            ZoomToRect(ZoomRect(size, CurrentView(), z, { size.width / 2, size.height / 2 }));
        });
    }

    void Surface::FitWidth()
    {
        WhenReady([this] {
            SizeF size = ClientSize();
            View view = CurrentView();
            float z = ClampZoom(FitWidthZoom(layout.DocumentSize().width, size.width, scale), options.minZoom, options.maxZoom);
            float top = -view.dy / view.zoom;
            ZoomToRect({ 0, top, size.width / z, top + size.height / z });
        });
    }

    void Surface::ScrollToPage(int page)
    {
        if (page < 0 || page >= layout.Count()) return;
        WhenReady([this, page] {
            float top = std::max(0.f, layout.Page(page).top - options.gap / 2);
            ZoomToRect(ScrollRect(ClientSize(), CurrentView(), scale, top));
        });
    }

    void Surface::SetTool(ToolState state)
    {
        ToolSettings settings{ state.tool, state.wholeStrokeErasing, state.eraserRadius, state.selectMode,
            ShapeLook{ state.shape, state.color, state.width }, state.recognizeShapes };
        PenLook look{ state.tool == Tool::Highlighter ? Tool::Highlighter : Tool::Pen, state.color, state.width };
        bool pens = state.tool == Tool::Pen || state.tool == Tool::Highlighter;
        if (state.tool != Tool::Select) selection->Clear();
        host.Post("tool", [this, settings, look, pens, cursor = state.cursor] {
            if (pens) ink->SetPen(look);
            ink->SetCursor(cursor);
            if (tools) tools->SetTool(settings);
        });
    }

    void Surface::SetRuler(bool shown)
    {
        RectF v = Visible();
        PointF at{ v.left + v.Width() / 8, (v.top + v.bottom) / 2 }; // in view, from the left
        host.Post("ruler", [this, shown, at] { if (tools) tools->Stencil().ShowRuler(shown, at); });
    }

    void Surface::SetProtractor(bool shown)
    {
        RectF v = Visible();
        PointF at{ (v.left + v.right) / 2, (v.top + v.bottom) / 2 };
        host.Post("protractor", [this, shown, at] { if (tools) tools->Stencil().ShowProtractor(shown, at); });
    }

    void Surface::SelectAll()
    {
        if (auto page = PageInView()) selection->SelectAll(*page);
    }

    void Surface::Undo()
    {
        selection->Clear(); // the frame would no longer fit what undo changes
        host.Post("undo", [this] { ink->Edits().Undo(); });
    }

    void Surface::Redo()
    {
        selection->Clear();
        host.Post("redo", [this] { ink->Edits().Redo(); });
    }

    void Surface::MarkSaved()
    {
        host.Post("saved", [this] { ink->Edits().MarkSaved(); });
    }

    std::vector<KeyedStroke> Surface::StrokesOf(int page)
    {
        auto strokes = std::make_shared<std::vector<KeyedStroke>>(); // outlives a timed-out wait
        host.Run("snapshot", [this, page, strokes] { if (pageStore) *strokes = pageStore->Snapshot(page); });
        return *strokes;
    }

    void Surface::InvalidatePage(int page)
    {
        composition->Invalidate(page);
        composition->UpdateSheets(Visible(), sheetScale);
        composition->Commit();
    }

    void Surface::Hold(int page, bool hold)
    {
        if (hold) held.insert(page);
        else held.erase(page);
        Changed(!viewport->Moving());
    }
}
