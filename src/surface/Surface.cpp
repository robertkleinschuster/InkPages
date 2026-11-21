#include "surface/Surface.h"
#include "inkpages/model/PaperAppearance.h"
#include "WindowData.h"

#include <cmath>
#include <format>
#include <utility>

#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.ViewManagement.h>

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
        constexpr UINT_PTR MotionTimer = 2;
        constexpr UINT MotionIntervalMs = 120;       // lazy pages and sheets while the view moves
        constexpr float RevealMarginDip = 16;

    }

    Surface::Surface(HWND parent, SurfaceOptions options) : options(options)
    {
        try
        {
            Initialize(parent);
        }
        catch (...)
        {
            if (window) SetWindowData(window, nullptr);
            Close();
            throw;
        }
    }

    void Surface::Initialize(HWND parent)
    {
        static bool registered = [] {
            WNDCLASSEXW wc{ sizeof(wc) };
            wc.style = CS_DBLCLKS; // a double tap selects a word in the edited text
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
        if (!window) winrt::throw_last_error();
        ui = std::make_unique<UiThread>();
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
            if (textEditor) textEditor->ViewChanged();
            if (viewChanged) viewChanged();
        };
        viewport->onStatus = [this](bool settled) {
            if (viewport->Moving())
            {
                if (touchTaps) touchTaps->Cancel("the view moves"); // a pan is no tap
                SetTimer(window, MotionTimer, MotionIntervalMs, nullptr);
            }
            if (!settled) return;
            KillTimer(window, MotionTimer);
            Changed(true);
            if (std::exchange(revealCaretOnSettle, false)) RevealTextCaret();
            auto run = std::move(pending);
            pending.clear();
            for (auto& action : run) action();
        };

        overlay.changed = [this] {
            if (overlayPending.exchange(true)) return; // one drawing per batch of changes
            try
            {
                if (ui->Post([this] { DrawOverlay(); })) return;
            }
            catch (...)
            {
                overlayPending = false;
                throw;
            }
            overlayPending = false;
        };

        selection = std::make_unique<SelectionController>(SelectionContext{ window, viewport.get(), &host, ui.get(),
            [this] { return ink.get(); }, [this] { return tools.get(); }, [this] { return pictureHost; },
            [this] { return scale; }, [this]() -> PageLayout const& { return layout; },
            [this] { if (selectionChanged) selectionChanged(); },
            [this](PointF at) { return textEditor && textEditor->Contains(at); }, this });

        auto toDocument = [this](POINT client) { return ToDocument(client); };
        auto zoom = [this] { return viewport->Current().zoom; };
        mouse = std::make_unique<MouseSelect>(window, host, [this] { return tools.get(); }, *selection, toDocument, zoom,
            [this](PointF at) { Tapped(at, PointerKind::Mouse); },
            [this](PointF at) {
                if (currentTool != Tool::Text) return false;
                textEditor->Tap(at, textStyle);
                return true;
            },
            MouseClaims{ [this](PointF at) { return AppClaims(at); },
                [this](MousePhase phase, PointF press, PointF at, int clicks) { AppGesture(phase, press, at, clicks); },
                [this](POINT press, POINT at) {
                    viewer->PanDown(press, false);
                    viewer->PanMove(at);
                } });
        touchTaps = std::make_unique<TouchTaps>(toDocument, zoom, [this](PointF at) { TouchTapped(at); }, [this](POINT screen) {
            if (!touchHeld || currentTool == Tool::Select || currentTool == Tool::Text) return;
            POINT client = screen;
            winrt::check_bool(ScreenToClient(window, &client));
            PointF doc = ToDocument(client);
            auto page = layout.PageAt(doc);
            if (!page) return;
            RectF paper = layout.Page(*page);
            ui->Post([this, page = *page, at = PointF{ doc.x - paper.left, doc.y - paper.top }, screen,
                generation = pagesGeneration.load()] {
                if (generation == pagesGeneration && touchHeld) touchHeld(page, at, screen);
            });
        });
        viewer = std::make_unique<ViewerInput>(ViewerInputContext{ window, viewport.get(), [this] { return ClientSize(); },
            [this] { return scale; }, [this]() -> PageLayout const& { return layout; },
            [this](RectF content) { ZoomToRect(content); }, toDocument,
            [this](int page, PointF point) -> LPCWSTR {
                if (page < 0) return cursorAt ? cursorAt(page, point) : nullptr;
                RectF paper = layout.Page(page);
                LPCWSTR toolCursor = selection->CursorAt({ paper.left + point.x, paper.top + point.y });
                if (!toolCursor && (currentTool == Tool::Select || currentTool == Tool::Laser)) toolCursor = IDC_CROSS;
                if (!toolCursor && currentTool == Tool::Text) toolCursor = IDC_IBEAM;
                if (!toolCursor) return cursorAt ? cursorAt(page, point) : nullptr;
                if (cursorAt) cursorAt(-1, {}); // close the app's link tooltip while the tool owns the cursor
                return toolCursor;
            },
            [this] {
                if (currentTool != Tool::Select && currentTool != Tool::Text && mousePannedWithWriteTool)
                    mousePannedWithWriteTool();
            } });

        automation = std::make_unique<SurfaceAutomation>(window);
        pageMark = std::make_unique<PageMark>(composition->Devices(), composition->MarkLayer());
        textEditor = std::make_unique<TextEditor>(TextEditorContext{ window, &composition->Devices(),
            composition->TextLayer(), &host, ui.get(), [this] { return ink.get(); }, [this] { return tools.get(); },
            [this] { return textHost; }, [this] { return CurrentView(); }, [this] { return scale; },
            [this] { return sheetScale; }, [this]() -> PageLayout const& { return layout; },
            [this] { composition->Commit(); }, automation.get(), [this](int page) {
                return darkPaper && page >= 0 && page < static_cast<int>(pages.size()) && pages[page].allowDarkPaper;
            } });
        textEditor->editingChanged = [this](bool editing) { EditChanged(editing); };
        textEditor->caretMoved = [this] { RevealTextCaret(); };

        ink = std::make_unique<InkLayer>(host,
            [this] { OnPresenterReady(); }, [this] { ui->Post([this] { composition->Commit(); }); });
        ink->Start(composition->Ink());
        Resized();
        composition->Commit();
        Log(std::format("surface: created (DPI scale {:.2f})", scale));
    }

    Surface::~Surface() { Close(); }

    void Surface::Close()
    {
        if (ui) ui->Close();
        try
        {
            if (uiSettings) uiSettings.ColorValuesChanged(accentChanged);
            inputPane.reset();
            inputPaneScreen.reset();
            textEditor.reset(); // ends editing first: the result still reaches the app and the history
            if (ink)
            {
                host.Run("tools shutdown", [this] {
                    ink->UnsubscribeStrokes();
                    tools.reset();
                    pageStore.reset();
                }, INFINITE); // the work refers to this surface: it must have run before the members go
            }
            ink.reset();
            mouse.reset();
            touchTaps.reset();
            viewer.reset();
            selection.reset();
            viewport.reset();
            pageMark.reset();
            composition.reset();
            automation.reset();
        }
        catch (...)
        {
            LogException("surface shutdown");
        }
        if (IsWindow(window))
        {
            SetWindowData(window, nullptr);
            if (!DestroyWindow(window)) LogHr("DestroyWindow (writing surface)", HRESULT_FROM_WIN32(GetLastError()));
        }
    }

    PointF Surface::ToDocument(POINT client) const
    {
        return ViewportToDocument({ static_cast<float>(client.x), static_cast<float>(client.y) }, viewport->Current(), scale);
    }

    void Surface::Tapped(PointF document, PointerKind kind)
    {
        if (kind == PointerKind::Touch && textEditor->Contains(document))
        {
            textEditor->PlaceCaret(document); // a finger tap into the edited text is the text's, not the app's
            return;
        }
        auto page = layout.PageAt(document);
        if (!page || !tapped) return; // beside the pages
        RectF r = layout.Page(*page);
        tapped(*page, { document.x - r.left, document.y - r.top }, kind);
    }

    bool Surface::AppClaims(PointF document) const
    {
        // Only where nothing of the library's lies: the selection's frame, the app's pictures, the edited text box.
        if (!mouseClaims || selection->Grabs(document) || (textEditor && textEditor->Contains(document))) return false;
        if (pictureHost && pictureHost->PictureAt(document)) return false;
        auto page = layout.PageAt(document);
        if (!page) return false;
        RectF r = layout.Page(*page);
        return mouseClaims(*page, { document.x - r.left, document.y - r.top });
    }

    void Surface::TouchTapped(PointF document)
    {
        if (selection->HasSelection() && !selection->Grabs(document))
        {
            selection->Clear();
            return;
        }
        if (currentTool == Tool::Text)
        {
            if (textEditor->Contains(document)) textEditor->PlaceCaret(document);
            else if (layout.PageAt(document)) textEditor->Tap(document, textStyle);
            return;
        }
        if (currentTool != Tool::Select)
        {
            Tapped(document, PointerKind::Touch);
            return;
        }
        if (selection->Grabs(document)) return;
        if (!layout.PageAt(document))
        {
            selection->Clear();
            return;
        }
        float radius = Tap::MaxDistance(viewport->Current().zoom);
        host.Post("touch select", [this, document, radius, generation = pagesGeneration.load()] {
            if (!tools) return;
            auto made = tools->Selection().SelectAt(document, radius);
            ui->Post([this, made, document, generation] {
                if (generation != pagesGeneration) return;
                selection->Selected(made, { document });
                if (!selection->HasSelection()) Tapped(document, PointerKind::Touch);
            });
        });
    }

    void Surface::AppGesture(MousePhase phase, PointF press, PointF document, int clicks)
    {
        auto page = layout.PageAt(press);
        if (!page || !mouseGesture) return; // (a claimed press lies on a page)
        RectF r = layout.Page(*page);
        mouseGesture({ phase, *page, { document.x - r.left, document.y - r.top }, clicks });
    }

    // Ink thread, once: the tools and the lazy pages need the presenter.
    void Surface::OnPresenterReady()
    {
        pageStore = std::make_unique<PageStore>(*ink);
        pageStore->load = [this](int page) { return load ? load(page) : std::vector<KeyedStroke>{}; };
        pageStore->store = [this](int page, std::vector<KeyedStroke> strokes) { if (store) store(page, std::move(strokes)); };
        tools = std::make_unique<ToolInput>(*ink, overlay);
        tools->selected = [this](StrokeSelection const& made, std::vector<PointF> const& polygon, bool rectangle) {
            ui->Post([this, made, polygon, rectangle, generation = inkPagesGeneration.load()] {
                if (generation != pagesGeneration) return; // made on pages that changed since
                textEditor->End(); // one thing at a time: a selection ends editing
                selection->Selected(made, polygon);
                if (!emptyRectangle || !rectangle || selection->HasSelection()) return;
                auto page = layout.PageAt(polygon.front());
                if (!page) return;
                RectF paper = layout.Page(*page);
                PointF a = polygon.front(), b = polygon[2];
                RectF area{
                    std::max(paper.left, std::min(a.x, b.x)) - paper.left,
                    std::max(paper.top, std::min(a.y, b.y)) - paper.top,
                    std::min(paper.right, std::max(a.x, b.x)) - paper.left,
                    std::min(paper.bottom, std::max(a.y, b.y)) - paper.top,
                };
                float minimum = Tap::MaxDistance(viewport->Current().zoom);
                if (area.Width() >= minimum && area.Height() >= minimum) emptyRectangle(*page, area);
            });
        };
        tools->cleared = [this] { ui->Post([this] { selection->Cleared(); }); };
        tools->penTaken = [](bool) {};
        tools->zoom = [this] {
            std::scoped_lock guard(viewLock);
            return inkView.zoom;
        };
        tools->textTapped = [this](PointF at) {
            ui->Post([this, at, generation = inkPagesGeneration.load()] {
                if (generation == pagesGeneration) textEditor->Tap(at, textStyle);
            });
        };
        ink->Edits().changed = [this](HistoryState state) {
            ui->Post([this, state] {
                Changed(!viewport->Moving()); // pages with history keep their ink
                if (historyChanged) historyChanged();
                if (contentChanged && !state.changed.empty()) contentChanged(state.changed);
            });
        };
        ink->collected = [this, previous = ink->collected](std::vector<Placed> const& placed) {
            if (previous) previous(placed);
            if (!placed.empty() && !reportedPenStroke)
            {
                reportedPenStroke = true;
                ui->Post([this] { if (firstPenStroke) firstPenStroke(); });
            }
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
        RECT rc = EffectiveClient();
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
        RECT rc = EffectiveClient();
        if (viewport) viewport->SetViewportRect(rc);
        composition->SetClip({ rc.right, rc.bottom });
        Changed(true);
    }

    RECT Surface::EffectiveClient() const
    {
        RECT client{};
        winrt::check_bool(GetClientRect(window, &client));
        if (auto pane = InputPaneInClient())
            client.bottom = static_cast<LONG>(ViewportWithInputPane(
                { static_cast<float>(client.right), static_cast<float>(client.bottom) }, *pane).height);
        return client;
    }

    std::optional<RectF> Surface::InputPaneInClient() const
    {
        if (!inputPaneScreen) return std::nullopt;
        POINT topLeft{ inputPaneScreen->left, inputPaneScreen->top };
        POINT bottomRight{ inputPaneScreen->right, inputPaneScreen->bottom };
        winrt::check_bool(ScreenToClient(window, &topLeft));
        winrt::check_bool(ScreenToClient(window, &bottomRight));
        return RectF{ static_cast<float>(topLeft.x), static_cast<float>(topLeft.y),
            static_cast<float>(bottomRight.x), static_cast<float>(bottomRight.y) };
    }

    void Surface::EditChanged(bool editing)
    {
        try
        {
            if (editing)
            {
                if (!inputPane)
                {
                    HWND root = GetAncestor(window, GA_ROOT);
                    winrt::check_pointer(root);
                    inputPane = std::make_unique<InputPane>(root, [this](std::optional<RECT> screen) { InputPaneChanged(screen); });
                }
                InputPaneChanged(inputPane->Location());
            }
            else
            {
                inputPane.reset(); // unregister while the root window is still alive
                if (inputPaneScreen) InputPaneChanged(std::nullopt);
            }
        }
        catch (...)
        {
            LogException("text: tracking the input pane");
        }
        if (textEditingChanged) textEditingChanged(editing);
    }

    void Surface::InputPaneChanged(std::optional<RECT> screen)
    {
        bool editing = textEditor && textEditor->Editing();
        if (!editing) screen.reset();
        if (!screen && !inputPaneScreen) return;
        inputPaneScreen = screen;
        Resized();
        RevealTextCaret();
    }

    void Surface::RevealTextCaret()
    {
        auto pane = InputPaneInClient();
        if (!pane || !textEditor || !viewport) return;
        if (viewport->Moving())
        {
            revealCaretOnSettle = true;
            return;
        }
        auto caret = textEditor->CaretInClient();
        if (!caret) return;
        SizeF doc = layout.DocumentSize();
        if (auto target = RevealCaretRect(ClientSize(), CurrentView(), { doc.width * scale, doc.height * scale },
            *caret, *pane, RevealMarginDip * scale, options.maxZoom))
            viewport->ZoomToRect(*target, false);
    }

    std::vector<bool> Surface::DarkPages() const
    {
        std::vector<bool> flags;
        flags.reserve(pages.size());
        for (auto const& page : pages) flags.push_back(darkPaper && page.allowDarkPaper);
        return flags;
    }

    Paper Surface::DisplayPaper(PageSpec const& page) const
    {
        Paper paper = page.paper;
        if (darkPaper && page.allowDarkPaper)
        {
            paper.sheetColor = PaperAppearance::DarkSheet;
            paper.lineColor = { paper.lineColor.a, 67, 67, 67 };
        }
        return paper;
    }

    void Surface::SetDarkPaper(bool dark)
    {
        if (darkPaper == dark) return;
        darkPaper = dark;
        textEditor->AppearanceChanged();
        winrt::check_hresult(host.Post("ink display palette", [this, flags = DarkPages()] {
            ink->SetDarkPages(std::move(flags));
            ui->Post([this] { composition->Commit(); });
        }));
        for (int i = 0; i < static_cast<int>(pages.size()); ++i)
            if (pages[i].allowDarkPaper) composition->SetPaper(i, DisplayPaper(pages[i]));
        if (sheetScale > 0) composition->UpdateSheets(Visible(), sheetScale);
        composition->Commit();
    }

    void Surface::BuildLayout(std::vector<PageSpec> specs, std::vector<Paper>& papers)
    {
        pages = std::move(specs);
        std::vector<SizeF> sizes;
        for (PageSpec const& p : pages)
        {
            sizes.push_back(p.size);
            papers.push_back(DisplayPaper(p));
        }
        layout = PageLayout(sizes, options.gap, options.margin);
    }

    void Surface::InsertPages(int index, std::vector<PageSpec> added)
    {
        index = std::clamp(index, 0, static_cast<int>(pages.size()));
        auto specs = pages;
        specs.insert(specs.begin() + index, added.begin(), added.end());
        Rearrange(std::move(specs), PageMap::Insert(static_cast<int>(pages.size()), index, static_cast<int>(added.size())));
    }

    void Surface::RemovePage(int index)
    {
        if (index < 0 || index >= static_cast<int>(pages.size())) return;
        auto specs = pages;
        specs.erase(specs.begin() + index);
        Rearrange(std::move(specs), PageMap::Remove(static_cast<int>(pages.size()), index));
    }

    void Surface::MovePage(int from, int to)
    {
        int count = static_cast<int>(pages.size());
        if (from < 0 || from >= count || to < 0 || to >= count || from == to) return;
        auto specs = pages;
        PageSpec moving = specs[from];
        specs.erase(specs.begin() + from);
        specs.insert(specs.begin() + to, moving);
        Rearrange(std::move(specs), PageMap::Move(count, from, to));
    }

    void Surface::Rearrange(std::vector<PageSpec> specs, PageMap const& map)
    {
        textEditor->End();
        selection->Clear();
        std::vector<Paper> papers;
        BuildLayout(std::move(specs), papers);
        held = map.Apply(held);
        live = map.Apply(live);
        ++pagesGeneration; // selections made on the old pages are dropped
        host.Post("rearrange pages", [this, map, layout = layout, flags = DarkPages()] {
            ++inkPagesGeneration;
            if (tools) // a gesture running on the old pages ends, before the history follows the pages
            {
                tools->Cancel();
                tools->Selection().Clear();
            }
            auto& state = ink->State();
            state.BeginRelocation(map, layout);
            ink->Edits().RemapPages(map);
            state.EndRelocation();
            ink->SetDarkPages(std::move(flags));
            if (pageStore) pageStore->Remap(map);
            // The app's stored strokes follow in the same work item: load/store queued before it carry the old
            // indices, those after it the new ones.
            if (pagesRemapped) pagesRemapped(map);
            ink->SetLayout(layout);
        });
        composition->Rearrange(layout, papers, map, [this](int page, ID2D1DeviceContext* dc, RectF area) {
            if (drawUnderlay) drawUnderlay(page, dc, area);
        });
        sheetScale = 0;
        SetScale();
        Log(std::format("surface: pages rearranged, {} pages", layout.Count()));
        Changed(true);
    }

    void Surface::SetPages(std::vector<PageSpec> specs)
    {
        textEditor->End();
        std::vector<Paper> papers;
        BuildLayout(std::move(specs), papers);
        selection->Clear();
        live.clear();
        held.clear();
        ++pagesGeneration;
        host.Post("pages", [this, layout = layout, flags = DarkPages()] {
            ++inkPagesGeneration;
            if (tools)
            {
                tools->Cancel();
                tools->Selection().Clear();
            }
            if (pageStore) pageStore->ReleaseAll(); // the old document's ink goes to `store`
            ink->Edits().Clear();
            ink->SetDarkPages(std::move(flags));
            ink->SetLayout(layout);
            overlay.ClearLaser();
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
        float currentScale = scale * CurrentView().zoom;
        // During a large zoom-out, refresh once the old surface is more than double the needed resolution.
        // At rest draw on the final pixel grid, ignoring tiny transform noise from panning.
        if (sheetScale <= 0 || currentScale < sheetScale * 0.5f ||
            (settled && std::abs(currentScale - sheetScale) > currentScale * 0.0001f))
            sheetScale = currentScale;
        composition->UpdateSheets(visible, sheetScale);
        ShowPageMark();
        if (sheetScale != before)
        {
            DrawOverlay();
            if (renderScaleChanged) renderScaleChanged();
            if (textEditor) textEditor->ViewChanged();
        }
        if (presenterReady)
        {
            auto visiblePage = layout.PageInView(visible);
            if (penViewPage != visiblePage)
            {
                penViewPage = visiblePage;
                host.Post("ink page in view", [this, visiblePage] { ink->SetViewPage(visiblePage); });
            }
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

    void Surface::MarkCurrentPage(bool mark)
    {
        if (mark && !uiSettings)
        {
            // The accent as Windows' settings have it, and again when the user changes it (the event comes on a worker
            // thread: the UI thread reads and draws).
            winrt::Windows::UI::ViewManagement::UISettings settings;
            accentChanged = settings.ColorValuesChanged([dispatch = ui->Dispatcher(), this](auto&&, auto&&) {
                if (auto queue = dispatch.lock())
                    queue->Post([this] {
                        ReadAccent();
                        ShowPageMark();
                        composition->Commit();
                    });
            });
            uiSettings = std::move(settings);
            ReadAccent();
        }
        markCurrentPage = mark;
        ShowPageMark();
        composition->Commit();
    }

    // WinUI drew the mark with AccentFillColorDefaultBrush: on the light paper that is the accent's first darker shade.
    void Surface::ReadAccent()
    {
        auto c = uiSettings.GetColorValue(winrt::Windows::UI::ViewManagement::UIColorType::AccentDark1);
        accent = { c.A, c.R, c.G, c.B };
    }

    void Surface::ShowPageMark()
    {
        std::optional<RectF> page;
        if (markCurrentPage && sheetScale > 0)
        {
            if (auto current = PageInView()) page = layout.Page(*current);
        }
        pageMark->Show(page, accent, sheetScale);
    }

    void Surface::DrawOverlay()
    {
        overlayPending = false;
        auto content = overlay.Get();
        bool fading = !content.laser.empty();
        if (fading && !laserTimerActive) winrt::check_bool(SetTimer(window, LaserTimer, 40, nullptr) != 0);
        if (!fading && laserTimerActive) winrt::check_bool(KillTimer(window, LaserTimer));
        laserTimerActive = fading;
        composition->Overlay().Draw(content, sheetScale > 0 ? sheetScale : scale);
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

    void Surface::FitPage()
    {
        if (layout.Count() == 0) return;
        WhenReady([this] {
            RectF page = layout.Page(PageInView().value_or(0));
            ZoomToRect(FitPageRect(page, ClientSize(), scale, options.gap / 2, options.minZoom, options.maxZoom));
        });
    }

    std::optional<ViewPlace> Surface::CurrentPlace() const
    {
        return PlaceOf(layout, Visible(), CurrentView().zoom);
    }

    void Surface::GoTo(ViewPlace place)
    {
        if (layout.Count() == 0) return;
        WhenReady([this, place] {
            Log(std::format("surface: to page {} at {:.0f},{:.0f}, zoom {:.2f}", place.page + 1, place.point.x, place.point.y, place.zoom));
            ZoomToRect(PlaceRect(layout, place, ClientSize(), scale, options.minZoom, options.maxZoom));
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

    void Surface::ScrollTo(int page, PointF point)
    {
        if (page < 0 || page >= layout.Count()) return;
        WhenReady([this, page, point] {
            RectF r = layout.Page(page);
            ZoomToRect(ScrollToPointRect(ClientSize(), CurrentView(), scale, { r.left + point.x, r.top + point.y },
                RevealMarginDip * scale));
        });
    }

    void Surface::RevealRect(int page, RectF area)
    {
        if (page < 0 || page >= layout.Count()) return;
        WhenReady([this, page, area] {
            RectF r = layout.Page(page);
            RectF doc{ r.left + area.left, r.top + area.top, r.left + area.right, r.top + area.bottom };
            if (auto to = inkpages::RevealRect(ClientSize(), CurrentView(), scale, doc, RevealMarginDip * scale)) ZoomToRect(*to);
        });
    }

    void Surface::SetTool(ToolState state)
    {
        if (state.tool == Tool::Shape)
        {
            state.tool = Tool::Pen;
            state.shapeEnabled = true;
        }
        bool pens = IsDrawingTool(state.tool);
        ToolSettings settings{ state.tool, state.wholeStrokeErasing, state.eraserRadius, state.selectMode,
            ShapeLook{ state.shape, state.color, state.width, state.tool == Tool::Highlighter },
            state.recognizeShapes, pens && state.shapeEnabled, state.squareEraser, state.laserSeconds };
        PenLook look{ state.tool == Tool::Highlighter ? Tool::Highlighter : Tool::Pen, state.color, state.width };
        textStyle = state.text;
        currentTool = state.tool;
        if (state.tool == Tool::Text) textEditor->SetStyle(state.text); // the edited text takes the menu's choice
        else textEditor->End();
        host.Post("tool", [this, settings, look, pens, cursor = state.cursor, darkCursor = state.darkCursor] {
            if (pens) ink->SetPen(look);
            ink->SetCursor(cursor, darkCursor);
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

    void Surface::EndTextEditing()
    {
        if (!textEditor || !textEditor->Editing()) return;
        textEditor->End();
        // The text's edit goes to the history on the ink thread (posted): wait for it, so AtSavePoint/CanUndo answer
        // with the text in (an app asks "save changes?" right after this).
        host.Run("text ended", [] {});
    }

    void Surface::SetTextHost(TextHost* textsOfApp)
    {
        textEditor->End();
        textHost = textsOfApp;
    }

    void Surface::Undo()
    {
        textEditor->End(); // the edit first becomes a step of its own
        selection->Clear(); // the frame would no longer fit what undo changes
        host.Post("undo", [this] { ink->Edits().Undo(); });
    }

    void Surface::Redo()
    {
        textEditor->End();
        selection->Clear();
        host.Post("redo", [this] { ink->Edits().Redo(); });
    }

    void Surface::MarkSaved()
    {
        host.Post("saved", [this] { ink->Edits().MarkSaved(); });
    }

    std::optional<std::vector<KeyedStroke>> Surface::StrokesOf(int page)
    {
        // Shared with the work: a timed-out wait leaves it running later. Throws on a timeout or the ink thread's error.
        auto strokes = std::make_shared<std::optional<std::vector<KeyedStroke>>>();
        host.Run("snapshot", [this, page, strokes] {
            if (pageStore && pageStore->IsLive(page)) *strokes = pageStore->Snapshot(page);
        });
        return *strokes;
    }

    void Surface::InvalidatePage(int page)
    {
        composition->Invalidate(page);
        composition->UpdateSheets(Visible(), sheetScale);
        composition->Commit();
    }

    void Surface::InvalidatePages(std::set<int> const& changedPages)
    {
        if (changedPages.empty()) return;
        for (int page : changedPages) composition->Invalidate(page);
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
