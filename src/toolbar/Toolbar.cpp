#include "inkpages/Toolbar.h"

#include <commctrl.h>

#include <algorithm>
#include <format>

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
        constexpr Color Dark{ 0xFF, 0x20, 0x20, 0x20 };

        // The comctl32 toolbar in a container window of the library's own (its notifications stay in the library).
        class Bar final : public Toolbar
        {
        public:
            Bar(HWND parent, WritingSurface& surface, ToolChoices choices, ToolbarTexts texts,
                std::function<UINT(Tool, PenPreset const&)> cursorFor, std::vector<ToolbarButton> extensions);
            ~Bar() override;

            HWND Window() const override { return container; }
            int Height() const override;
            ToolChoices const& Choices() const override { return choices; }
            void Choose(ToolChoices next) override;
            void EnableButton(int id, bool enabled) override;

            static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wp, LPARAM lp);

        private:
            LRESULT Handle(UINT message, WPARAM wp, LPARAM lp);
            void Build();
            void Click(int id);
            void Menu(int id);
            void Apply(bool changed);
            void UpdateStates();
            SurfaceStatus Status() const;
            RECT ButtonRect(int id) const;

            WritingSurface& surface;
            ToolChoices choices;
            ToolbarTexts texts;
            std::function<UINT(Tool, PenPreset const&)> cursorFor;
            std::vector<ToolbarButton> extensions;
            StencilChoice stencils;
            HWND container = nullptr, bar = nullptr;
            HIMAGELIST images = nullptr;
        };

        Bar::Bar(HWND parent, WritingSurface& surface, ToolChoices choices, ToolbarTexts texts,
            std::function<UINT(Tool, PenPreset const&)> cursorFor, std::vector<ToolbarButton> extensions)
            : surface(surface), choices(std::move(choices)), texts(std::move(texts)), cursorFor(std::move(cursorFor)),
              extensions(std::move(extensions))
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
            winrt::check_pointer(container);
            bar = CreateWindowExW(0, TOOLBARCLASSNAMEW, L"", WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT | TBSTYLE_TOOLTIPS |
                CCS_NODIVIDER | CCS_NORESIZE | CCS_NOPARENTALIGN, 0, 0, 100, 40, container, nullptr,
                GetModuleHandleW(nullptr), nullptr);
            winrt::check_pointer(bar);
            SendMessageW(bar, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
            SendMessageW(bar, TB_SETEXTENDEDSTYLE, 0, TBSTYLE_EX_DRAWDDARROWS);
            Build();
            // Undo/redo and the selection's commands follow the surface (after the app's own handlers).
            for (auto* handler : { &surface.historyChanged, &surface.selectionChanged })
            {
                *handler = [this, previous = std::move(*handler)] {
                    UpdateStates();
                    if (previous) previous();
                };
            }
            Apply(false);
        }

        Bar::~Bar()
        {
            if (container)
            {
                SetWindowLongPtrW(container, GWLP_USERDATA, 0);
                DestroyWindow(container);
            }
            if (images) ImageList_Destroy(images);
        }

        void Bar::Build()
        {
            int size = MulDiv(16, GetDpiForWindow(container), 96); // normal mouse size (the pen operates it)
            if (images) ImageList_Destroy(images);
            images = ImageList_Create(size, size, ILC_COLOR32, 24, 4);
            while (SendMessageW(bar, TB_BUTTONCOUNT, 0, 0) > 0) SendMessageW(bar, TB_DELETEBUTTON, 0, 0);
            SendMessageW(bar, TB_SETIMAGELIST, 0, reinterpret_cast<LPARAM>(images));
            std::vector<TBBUTTON> buttons;
            for (ButtonSpec const& spec : ToolbarButtons(choices, extensions))
            {
                if (spec.groupStart)
                {
                    TBBUTTON separator{};
                    separator.fsStyle = BTNS_SEP;
                    buttons.push_back(separator);
                }
                HBITMAP icon = GlyphIcon(spec.glyph, Dark, spec.bar, size);
                TBBUTTON b{};
                b.iBitmap = ImageList_Add(images, icon, nullptr);
                DeleteObject(icon);
                b.idCommand = spec.id;
                b.fsState = TBSTATE_ENABLED;
                b.fsStyle = static_cast<BYTE>(spec.kind == ButtonKind::Command ? BTNS_BUTTON
                    : spec.kind == ButtonKind::Check ? BTNS_CHECK : BTNS_CHECK | BTNS_DROPDOWN);
                buttons.push_back(b);
            }
            SendMessageW(bar, TB_ADDBUTTONSW, buttons.size(), reinterpret_cast<LPARAM>(buttons.data()));
            SendMessageW(bar, TB_AUTOSIZE, 0, 0);
            UpdateStates();
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
                SendMessageW(bar, TB_ENABLEBUTTON, spec.id, IsEnabled(spec.id, status));
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
            ToolState state;
            state.tool = choices.tool;
            state.wholeStrokeErasing = choices.wholeStrokeErasing;
            state.eraserRadius = choices.eraserRadius;
            state.selectMode = choices.selectMode;
            state.shape = choices.shape;
            state.recognizeShapes = choices.recognizeShapes;
            // Shapes and recognition draw with the last pen preset's look.
            Tool look = choices.tool == Tool::Highlighter ? Tool::Highlighter : Tool::Pen;
            auto presets = look == Tool::Pen ? choices.PensOrDefault() : choices.HighlightersOrDefault();
            int index = choices.tool == Tool::Pen || choices.tool == Tool::Highlighter ? choices.preset : 0;
            index = std::clamp(index, 0, static_cast<int>(presets.size()) - 1);
            PenPreset preset = presets[index];
            state.color = look == Tool::Pen ? Palette::Pens[preset.color] : Palette::Highlighters[preset.color];
            state.width = preset.width;
            bool pens = choices.tool == Tool::Pen || choices.tool == Tool::Highlighter;
            state.cursor = cursorFor && pens ? cursorFor(choices.tool, preset) : 0;
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
            case ButtonId::Shape: choose(Tool::Shape, choices.preset); break;
            case ButtonId::Ruler:
                stencils.ruler = !stencils.ruler;
                surface.SetRuler(stencils.ruler);
                break;
            case ButtonId::Cut: surface.Cut(); break;
            case ButtonId::Copy: surface.Copy(); break;
            case ButtonId::Paste: surface.Paste(); break;
            case ButtonId::Duplicate: surface.Duplicate(); break;
            case ButtonId::Delete: surface.DeleteSelection(); break;
            case ButtonId::SelectAll: surface.SelectAll(); break;
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
            if (IsPreset(id, ButtonId::Pen)) next = PresetMenu(container, anchor, choices, Tool::Pen, id - ButtonId::Pen, texts);
            else if (IsPreset(id, ButtonId::Highlighter)) next = PresetMenu(container, anchor, choices, Tool::Highlighter, id - ButtonId::Highlighter, texts);
            else if (id == ButtonId::WholeStrokes) next = RadiusMenu(container, anchor, choices, texts);
            else if (id == ButtonId::Select) next = SelectModeMenu(container, anchor, choices, texts);
            else if (id == ButtonId::Shape) next = ShapeMenu(container, anchor, choices, texts);
            else if (id == ButtonId::Ruler)
            {
                if (auto chosen = StencilMenu(container, anchor, stencils, texts))
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
                SetWindowLongPtrW(window, GWLP_USERDATA,
                    reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams));
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

        LRESULT Bar::Handle(UINT message, WPARAM wp, LPARAM lp)
        {
            switch (message)
            {
            case WM_COMMAND:
                if (reinterpret_cast<HWND>(lp) == bar) Click(LOWORD(wp));
                return 0;
            case WM_NOTIFY:
            {
                auto header = reinterpret_cast<NMHDR*>(lp);
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
                return 0;
            }
            return DefWindowProcW(container, message, wp, lp);
        }
    }

    std::unique_ptr<Toolbar> Toolbar::Create(HWND parent, WritingSurface& surface, ToolChoices choices, ToolbarTexts texts,
        std::function<UINT(Tool, PenPreset const&)> cursorFor, std::vector<ToolbarButton> extensions)
    {
        return std::make_unique<Bar>(parent, surface, std::move(choices), std::move(texts), std::move(cursorFor),
            std::move(extensions));
    }
}
