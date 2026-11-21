#include "inkpages/Toolbar.h"
#include "inkpages/model/PaperAppearance.h"
#include "WindowData.h"

#include <commctrl.h>

#include <algorithm>
#include <format>
#include <type_traits>

#include "inkpages/Log.h"
#include "toolbar/Icons.h"
#include "toolbar/ToolbarMenus.h"
#include "toolbar/ToolbarModel.h"

namespace inkpages
{
    namespace
    {
        using namespace detail;

        constexpr wchar_t ClassName[] = L"InkPagesToolbar";
        constexpr Color NotebookBlue{ 0xFF, 0x21, 0x57, 0x8F };

        // The comctl32 toolbar in a container window of the library's own (its notifications stay in the library).
        class Bar final : public Toolbar
        {
        public:
            Bar(HWND parent, WritingSurface& surface, ToolChoices choices, ToolbarTexts texts,
                std::function<UINT(Tool, PenPreset const&)> cursorFor, std::vector<ToolbarButton> extensions,
                std::function<UINT(Tool, PenPreset const&)> darkCursorFor);
            ~Bar() override;

            HWND Window() const override { return container; }
            int Height() const override;
            ToolChoices const& Choices() const override { return choices; }
            void Choose(ToolChoices next) override;
            void EnableButton(int id, bool enabled) override;
            void SetColors(std::optional<ToolbarColors> palette) override;
            void SetLargeIcons(bool large) override;
            void SetDarkPaperPreview(bool dark) override;

            static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wp, LPARAM lp);

        private:
            LRESULT Handle(UINT message, WPARAM wp, LPARAM lp);
            void Build();
            void DestroyControls();
            void Click(int id);
            void Menu(int id);
            void Apply(bool changed);
            void UpdateStates();
            SurfaceStatus Status() const;
            RECT ButtonRect(int id) const;
            void DrawDropdownArrow(NMCUSTOMDRAW const& draw) const;
            HBITMAP IconOf(ButtonSpec const& spec, int size) const;
            int IconSize() const { return MulDiv(largeIcons ? 32 : 16, GetDpiForWindow(container), 96); }

            WritingSurface& surface;
            ToolChoices choices;
            ToolbarTexts texts;
            std::function<UINT(Tool, PenPreset const&)> cursorFor;
            std::function<UINT(Tool, PenPreset const&)> darkCursorFor;
            std::vector<ToolbarButton> extensions;
            StencilChoice stencils;
            std::vector<std::wstring> names; // the buttons' texts (accessible names), alive as long as the buttons
            HWND container = nullptr, bar = nullptr;
            HIMAGELIST images = nullptr;
            std::optional<ToolbarColors> colors;
            bool darkPaperPreview = false;
            bool largeIcons = false;
            std::shared_ptr<bool> alive = std::make_shared<bool>(true);
        };

        Bar::Bar(HWND parent, WritingSurface& surface, ToolChoices choices, ToolbarTexts texts,
            std::function<UINT(Tool, PenPreset const&)> cursorFor, std::vector<ToolbarButton> extensions,
            std::function<UINT(Tool, PenPreset const&)> darkCursorFor)
            : surface(surface), choices(std::move(choices)), texts(std::move(texts)), cursorFor(std::move(cursorFor)),
              darkCursorFor(std::move(darkCursorFor)), extensions(std::move(extensions))
        {
            static bool registered = [] {
                WNDCLASSEXW wc{ sizeof(wc) };
                wc.lpfnWndProc = WindowProc;
                wc.hInstance = GetModuleHandleW(nullptr);
                wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
                wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
                wc.lpszClassName = ClassName;
                winrt::check_bool(RegisterClassExW(&wc));
                return true;
            }();
            (void)registered;
            container = CreateWindowExW(0, ClassName, L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, 0, 0, 100, 40, parent,
                nullptr, GetModuleHandleW(nullptr), this);
            if (!container) winrt::throw_last_error();
            try
            {
                bar = CreateWindowExW(0, TOOLBARCLASSNAMEW, L"", WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT | TBSTYLE_LIST | TBSTYLE_TOOLTIPS |
                    CCS_NODIVIDER | CCS_NORESIZE | CCS_NOPARENTALIGN, 0, 0, 100, 40, container, nullptr,
                    GetModuleHandleW(nullptr), nullptr);
                if (!bar) winrt::throw_last_error();
                SendMessageW(bar, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
                // Mixed buttons keep accessible names while rendering the icons alone.
                SendMessageW(bar, TB_SETEXTENDEDSTYLE, 0, TBSTYLE_EX_DRAWDDARROWS | TBSTYLE_EX_MIXEDBUTTONS);
                Build();
                Apply(false);
                auto followSurface = [this](std::function<void()> const& previous) -> std::function<void()> {
                    return [this, lifetime = std::weak_ptr<bool>(alive), previous] {
                        if (!lifetime.expired()) UpdateStates();
                        if (previous) previous();
                    };
                };
                auto history = followSurface(surface.historyChanged);
                auto selection = followSurface(surface.selectionChanged);
                surface.historyChanged = std::move(history);
                surface.selectionChanged = std::move(selection);
            }
            catch (...)
            {
                DestroyControls();
                throw;
            }
        }

        Bar::~Bar()
        {
            alive.reset();
            DestroyControls();
        }

        void Bar::DestroyControls()
        {
            if (IsWindow(container))
            {
                SetWindowData(container, nullptr);
                if (!DestroyWindow(container)) LogHr("DestroyWindow (toolbar)", HRESULT_FROM_WIN32(GetLastError()));
            }
            if (images && !ImageList_Destroy(images)) LogError("ImageList_Destroy (toolbar) failed");
        }

        void Bar::Build()
        {
            int size = IconSize();
            std::unique_ptr<std::remove_pointer_t<HIMAGELIST>, decltype(&ImageList_Destroy)> nextImages(
                ImageList_Create(size, size, ILC_COLOR32, 24, 4), ImageList_Destroy);
            if (!nextImages) throw winrt::hresult_error(E_FAIL, L"ImageList_Create (toolbar)");
            std::vector<TBBUTTON> buttons;
            auto specs = ToolbarButtons(choices, extensions);
            std::vector<std::wstring> nextNames;
            nextNames.reserve(specs.size()); // the toolbar keeps pointers into these
            for (ButtonSpec const& spec : specs)
            {
                if (spec.groupStart)
                {
                    TBBUTTON separator{};
                    separator.fsStyle = BTNS_SEP;
                    buttons.push_back(separator);
                }
                TBBUTTON b{};
                HBITMAP icon = IconOf(spec, size);
                if (!icon) throw winrt::hresult_error(E_FAIL, L"Render toolbar icon");
                b.iBitmap = ImageList_Add(nextImages.get(), icon, nullptr);
                if (!DeleteObject(icon)) LogError("DeleteObject (toolbar bitmap) failed");
                if (b.iBitmap < 0) throw winrt::hresult_error(E_FAIL, L"ImageList_Add (toolbar)");
                b.idCommand = spec.id;
                b.fsState = TBSTATE_ENABLED;
                if (spec.id >= ButtonId::Extension)
                {
                    LRESULT state = SendMessageW(bar, TB_GETSTATE, spec.id, 0);
                    if (state >= 0 && !(state & TBSTATE_ENABLED)) b.fsState = 0;
                }
                nextNames.push_back(NameOf(spec.id, choices, texts, extensions));
                b.iString = reinterpret_cast<INT_PTR>(nextNames.back().c_str());
                b.fsStyle = static_cast<BYTE>(spec.kind == ButtonKind::Command ? BTNS_BUTTON
                    : spec.kind == ButtonKind::Check ? BTNS_CHECK : BTNS_CHECK | BTNS_DROPDOWN);
                buttons.push_back(b);
            }
            while (SendMessageW(bar, TB_BUTTONCOUNT, 0, 0) > 0)
            {
                if (!SendMessageW(bar, TB_DELETEBUTTON, 0, 0))
                    throw winrt::hresult_error(E_FAIL, L"TB_DELETEBUTTON (toolbar)");
            }
            if (!SendMessageW(bar, TB_SETBITMAPSIZE, 0, MAKELPARAM(size, size)))
                throw winrt::hresult_error(E_FAIL, L"TB_SETBITMAPSIZE (toolbar)");
            HIMAGELIST old = images;
            images = nextImages.release();
            SendMessageW(bar, TB_SETIMAGELIST, 0, reinterpret_cast<LPARAM>(images));
            if (old && !ImageList_Destroy(old)) LogError("ImageList_Destroy (previous toolbar images) failed");
            names.swap(nextNames);
            if (!SendMessageW(bar, TB_ADDBUTTONSW, buttons.size(), reinterpret_cast<LPARAM>(buttons.data())))
                throw winrt::hresult_error(E_FAIL, L"TB_ADDBUTTONS (toolbar)");
            SendMessageW(bar, TB_AUTOSIZE, 0, 0);
            UpdateStates();
        }

        HBITMAP Bar::IconOf(ButtonSpec const& spec, int size) const
        {
            if (spec.id >= ButtonId::Extension)
            {
                ToolbarButton const& b = extensions[spec.id - ButtonId::Extension];
                if (b.symbol) return ArtworkIcon(*b.symbol, {}, size);
                if (b.glyph) return GlyphIcon(b.glyph, NotebookBlue, {}, size);
                return PathIcon(b.path, b.pathSize, NotebookBlue, size);
            }
            auto accent = spec.bar;
            if (accent && darkPaperPreview)
                accent = PaperAppearance::DarkToolInk(*accent, IsPreset(spec.id, ButtonId::Highlighter));
            return ArtworkIcon(spec.symbol, accent, size);
        }

        int Bar::Height() const
        {
            SIZE size{};
            SendMessageW(bar, TB_GETMAXSIZE, 0, reinterpret_cast<LPARAM>(&size));
            return size.cy + MulDiv(4, GetDpiForWindow(container), 96);
        }

        void Bar::EnableButton(int id, bool enabled)
        {
            for (size_t i = 0; i < extensions.size(); ++i)
            {
                if (extensions[i].id == id) SendMessageW(bar, TB_ENABLEBUTTON, ButtonId::Extension + i, enabled);
            }
        }

        SurfaceStatus Bar::Status() const
        {
            return { surface.CanUndo(), surface.CanRedo(), surface.HasSelection(), stencils.ruler, stencils.protractor };
        }

        void Bar::UpdateStates()
        {
            SurfaceStatus status = Status();
            for (ButtonSpec const& spec : ToolbarButtons(choices, extensions))
            {
                if (spec.id >= ButtonId::Extension) continue; // the app enables its own
                SendMessageW(bar, TB_ENABLEBUTTON, spec.id, IsEnabled(spec.id, status) && (spec.id != ButtonId::Shape || IsDrawingTool(choices.tool)));
                SendMessageW(bar, TB_CHECKBUTTON, spec.id, IsChecked(spec.id, choices, status));
            }
        }

        void Bar::Choose(ToolChoices next)
        {
            choices = std::move(next);
            Build();
            Apply(true);
        }

        // The surface gets the tool the choices describe; `changed`: tell the app.
        void Bar::Apply(bool changed)
        {
            if (choices.tool == Tool::Shape)
            {
                choices.tool = Tool::Pen;
                choices.shapeEnabled = true;
            }
            ToolState state;
            state.tool = choices.tool;
            state.wholeStrokeErasing = choices.wholeStrokeErasing;
            state.eraserRadius = choices.eraserRadius;
            state.squareEraser = choices.squareEraser;
            state.selectMode = choices.selectMode;
            state.shape = choices.shape;
            state.shapeEnabled = choices.DrawingShape();
            state.recognizeShapes = choices.recognizeShapes;
            state.text = choices.Text();
            state.laserSeconds = choices.laserSeconds;
            Tool look = choices.tool == Tool::Highlighter ? Tool::Highlighter : Tool::Pen;
            auto presets = look == Tool::Pen ? choices.PensOrDefault() : choices.HighlightersOrDefault();
            int index = IsDrawingTool(choices.tool) ? choices.preset : 0;
            index = std::clamp(index, 0, static_cast<int>(presets.size()) - 1);
            PenPreset preset = presets[index];
            state.color = look == Tool::Pen ? Palette::Pens[preset.color] : Palette::Highlighters[preset.color];
            state.width = preset.width;
            bool pens = IsDrawingTool(choices.tool);
            state.cursor = cursorFor && pens ? cursorFor(choices.tool, preset) : 0;
            state.darkCursor = darkCursorFor && pens ? darkCursorFor(choices.tool, preset) : 0;
            surface.SetTool(state);
            UpdateStates();
            Log(std::format("toolbar: {}", choices.Serialize()));
            if (changed && choicesChanged) choicesChanged(choices);
        }

        void Bar::Click(int id)
        {
            auto choose = [this](Tool tool, int preset) {
                choices.tool = tool;
                choices.preset = preset;
                Apply(true);
            };
            if (IsPreset(id, ButtonId::Pen))
            {
                if (IsChecked(id, choices, Status())) return Menu(id); // a second tap: its options
                return choose(Tool::Pen, id - ButtonId::Pen);
            }
            if (IsPreset(id, ButtonId::Highlighter))
            {
                if (IsChecked(id, choices, Status())) return Menu(id);
                return choose(Tool::Highlighter, id - ButtonId::Highlighter);
            }
            if (id >= ButtonId::Extension)
            {
                size_t i = static_cast<size_t>(id - ButtonId::Extension);
                if (i < extensions.size() && extensions[i].click) extensions[i].click();
                return;
            }
            switch (id)
            {
            case ButtonId::Undo: surface.Undo(); break;
            case ButtonId::Redo: surface.Redo(); break;
            case ButtonId::Eraser: choose(Tool::Eraser, choices.preset); break;
            case ButtonId::WholeStrokes:
                choices.wholeStrokeErasing = !choices.wholeStrokeErasing; // a toggle: the tool stays
                Apply(true);
                break;
            case ButtonId::Select: choose(Tool::Select, choices.preset); break;
            case ButtonId::Shape:
                if (!IsDrawingTool(choices.tool)) return;
                choices.ToggleShape(choices.shape);
                Apply(true);
                break;
            case ButtonId::Laser: choose(Tool::Laser, choices.preset); break;
            case ButtonId::Text: choose(Tool::Text, choices.preset); break;
            case ButtonId::Ruler:
                stencils.ruler = !stencils.ruler;
                surface.SetRuler(stencils.ruler);
                break;
            case ButtonId::Cut: surface.Cut(); break;
            case ButtonId::Copy: surface.Copy(); break;
            case ButtonId::Paste: surface.Paste(); break;
            case ButtonId::Duplicate: surface.Duplicate(); break;
            case ButtonId::Delete: surface.DeleteSelection(); break;
            default: break;
            }
            UpdateStates();
        }

        RECT Bar::ButtonRect(int id) const
        {
            RECT r{};
            SendMessageW(bar, TB_GETRECT, id, reinterpret_cast<LPARAM>(&r));
            MapWindowPoints(bar, HWND_DESKTOP, reinterpret_cast<POINT*>(&r), 2);
            return r;
        }

        void Bar::Menu(int id)
        {
            RECT anchor = ButtonRect(id);
            std::optional<ToolChoices> next;
            if (IsPreset(id, ButtonId::Pen))
                next = PresetMenu(container, anchor, choices, Tool::Pen, id - ButtonId::Pen, texts, colors, darkPaperPreview);
            else if (IsPreset(id, ButtonId::Highlighter))
                next = PresetMenu(container, anchor, choices, Tool::Highlighter, id - ButtonId::Highlighter, texts, colors, darkPaperPreview);
            else if (id == ButtonId::Eraser)
            {
                if (auto chosen = RadiusMenu(container, anchor, choices, texts, colors))
                {
                    choices.eraserRadius = chosen->eraserRadius;
                    choices.squareEraser = chosen->squareEraser;
                    Apply(true);
                }
                return;
            }
            else if (id == ButtonId::Select) next = SelectModeMenu(container, anchor, choices, texts, colors);
            else if (id == ButtonId::Shape)
            {
                if (auto chosen = ShapeMenu(container, anchor, choices, texts, colors))
                {
                    choices = std::move(*chosen);
                    Apply(true);
                }
                return;
            }
            else if (id == ButtonId::Laser) next = LaserMenu(container, anchor, choices, colors);
            else if (id == ButtonId::Text) next = TextMenu(container, anchor, choices, texts, colors, darkPaperPreview);
            else if (id == ButtonId::Ruler)
            {
                if (auto chosen = StencilMenu(container, anchor, stencils, texts, colors))
                {
                    if (chosen->ruler != stencils.ruler) surface.SetRuler(chosen->ruler);
                    if (chosen->protractor != stencils.protractor) surface.SetProtractor(chosen->protractor);
                    stencils = *chosen;
                    UpdateStates();
                }
                return;
            }
            if (next) Choose(*next); // icons show the presets' colors
        }

        LRESULT CALLBACK Bar::WindowProc(HWND window, UINT message, WPARAM wp, LPARAM lp)
        {
            if (message == WM_NCCREATE)
            {
                if (!SetWindowData(window, reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams)) return FALSE;
            }
            auto self = reinterpret_cast<Bar*>(GetWindowLongPtrW(window, GWLP_USERDATA));
            if (self && self->bar)
            {
                try
                {
                    return self->Handle(message, wp, lp);
                }
                catch (...)
                {
                    LogException(std::format("toolbar: message 0x{:X}", message));
                    return 0;
                }
            }
            return DefWindowProcW(window, message, wp, lp);
        }

        void Bar::SetColors(std::optional<ToolbarColors> palette)
        {
            colors = palette;
            winrt::check_bool(RedrawWindow(container, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN));
        }

        void Bar::SetLargeIcons(bool large)
        {
            if (largeIcons == large) return;
            largeIcons = large;
            Build();
            if (sizeChanged) sizeChanged();
        }

        void Bar::SetDarkPaperPreview(bool dark)
        {
            if (darkPaperPreview == dark) return;
            darkPaperPreview = dark;
            int size = IconSize();
            for (auto const& spec : ToolbarButtons(choices, extensions))
            {
                if (!spec.bar) continue;
                TBBUTTONINFOW button{ sizeof(button), TBIF_IMAGE };
                if (SendMessageW(bar, TB_GETBUTTONINFOW, spec.id, reinterpret_cast<LPARAM>(&button)) < 0)
                    winrt::throw_hresult(E_FAIL);
                HBITMAP bitmap = IconOf(spec, size);
                if (!bitmap) winrt::throw_hresult(E_FAIL);
                BOOL replaced = ImageList_Replace(images, button.iImage, bitmap, nullptr);
                if (!DeleteObject(bitmap)) LogError("DeleteObject (toolbar preview bitmap)");
                if (!replaced) winrt::throw_hresult(E_FAIL);
            }
            winrt::check_bool(InvalidateRect(bar, nullptr, FALSE));
        }

        void Bar::DrawDropdownArrow(NMCUSTOMDRAW const& draw) const
        {
            TBBUTTONINFOW button{ sizeof(button), TBIF_STYLE };
            if (SendMessageW(bar, TB_GETBUTTONINFOW, draw.dwItemSpec, reinterpret_cast<LPARAM>(&button)) < 0 ||
                !(button.fsStyle & BTNS_DROPDOWN)) return;
            // Keep the control's split button and notification. Paint its indicator in the contrasting palette.
            UINT dpi = GetDpiForWindow(bar);
            int cx = draw.rc.right - MulDiv(9, dpi, 96);
            int cy = (draw.rc.top + draw.rc.bottom) / 2;
            int half = MulDiv(4, dpi, 96);
            int rise = MulDiv(2, dpi, 96);
            POINT arrow[] { { cx - half, cy - rise }, { cx + half, cy - rise }, { cx, cy + rise } };
            auto dc = draw.hdc;
            int saved = SaveDC(dc);
            if (!saved) winrt::throw_hresult(E_FAIL);
            SetDCBrushColor(dc, colors->text);
            SelectObject(dc, GetStockObject(DC_BRUSH));
            SelectObject(dc, GetStockObject(NULL_PEN));
            BOOL drawn = Polygon(dc, arrow, ARRAYSIZE(arrow));
            if (!RestoreDC(dc, saved)) LogError("RestoreDC (toolbar dropdown arrow)");
            if (!drawn) LogError("Polygon (toolbar dropdown arrow)");
        }

        LRESULT Bar::Handle(UINT message, WPARAM wp, LPARAM lp)
        {
            switch (message)
            {
            case WM_MEASUREITEM:
                if (colors && MeasureToolMenu(container, *reinterpret_cast<MEASUREITEMSTRUCT*>(lp))) return TRUE;
                break;
            case WM_DRAWITEM:
                if (colors && DrawToolMenu(container, *reinterpret_cast<DRAWITEMSTRUCT const*>(lp), *colors)) return TRUE;
                break;
            case WM_ERASEBKGND:
                if (colors)
                {
                    RECT rect{};
                    winrt::check_bool(GetClientRect(container, &rect));
                    auto dc = reinterpret_cast<HDC>(wp);
                    SetDCBrushColor(dc, colors->background);
                    winrt::check_bool(FillRect(dc, &rect, static_cast<HBRUSH>(GetStockObject(DC_BRUSH))));
                    return TRUE;
                }
                break;
            case WM_COMMAND:
                if (reinterpret_cast<HWND>(lp) == bar) Click(LOWORD(wp));
                return 0;
            case WM_NOTIFY:
            {
                auto header = reinterpret_cast<NMHDR*>(lp);
                if (colors && header->hwndFrom == bar && header->code == NM_CUSTOMDRAW)
                {
                    auto& draw = *reinterpret_cast<NMTBCUSTOMDRAW*>(lp);
                    if (draw.nmcd.dwDrawStage == CDDS_PREPAINT)
                    {
                        SetDCBrushColor(draw.nmcd.hdc, colors->background);
                        winrt::check_bool(FillRect(draw.nmcd.hdc, &draw.nmcd.rc,
                            static_cast<HBRUSH>(GetStockObject(DC_BRUSH))));
                        return CDRF_NOTIFYITEMDRAW;
                    }
                    if (draw.nmcd.dwDrawStage == CDDS_ITEMPREPAINT)
                    {
                        draw.clrText = colors->text;
                        draw.clrTextHighlight = colors->text;
                        draw.clrHighlightHotTrack = colors->highlight;
                        return TBCDRF_USECDCOLORS | TBCDRF_HILITEHOTTRACK | CDRF_NOTIFYPOSTPAINT;
                    }
                    if (draw.nmcd.dwDrawStage == CDDS_ITEMPOSTPAINT) DrawDropdownArrow(draw.nmcd);
                }
                if (header->code == TBN_DROPDOWN)
                {
                    Menu(reinterpret_cast<NMTOOLBARW*>(lp)->iItem);
                    return TBDDRET_DEFAULT;
                }
                if (header->code == TBN_GETINFOTIPW)
                {
                    auto tip = reinterpret_cast<NMTBGETINFOTIPW*>(lp);
                    std::wstring text = TipOf(tip->iItem, choices, texts, extensions);
                    wcsncpy_s(tip->pszText, tip->cchTextMax, text.c_str(), _TRUNCATE);
                    return 0;
                }
                break;
            }
            case WM_SIZE:
                MoveWindow(bar, 0, MulDiv(2, GetDpiForWindow(container), 96), LOWORD(lp), HIWORD(lp), TRUE);
                return 0;
            case WM_DPICHANGED_AFTERPARENT:
                Build();
                if (sizeChanged) sizeChanged();
                return 0;
            }
            return DefWindowProcW(container, message, wp, lp);
        }
    }

    std::unique_ptr<Toolbar> Toolbar::Create(HWND parent, WritingSurface& surface, ToolChoices choices, ToolbarTexts texts,
        std::function<UINT(Tool, PenPreset const&)> cursorFor, std::vector<ToolbarButton> extensions,
        std::function<UINT(Tool, PenPreset const&)> darkCursorFor)
    {
        return std::make_unique<Bar>(parent, surface, std::move(choices), std::move(texts), std::move(cursorFor),
            std::move(extensions), std::move(darkCursorFor));
    }
}
