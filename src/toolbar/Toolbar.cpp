#include "inkpages/Toolbar.h"

#include <commctrl.h>

#include <algorithm>
#include <cmath>
#include <format>

#include "toolbar/Icons.h"
#include "inkpages/Log.h"

namespace inkpages
{
    namespace
    {
        using namespace detail;

        constexpr wchar_t ClassName[] = L"InkPagesToolbar";
        constexpr int UndoId = 100, RedoId = 101, PenId = 200, HighlighterId = 300, EraserId = 400, WholeId = 401,
            ExtensionId = 1000;
        constexpr wchar_t UndoGlyph = 0xE7A7, RedoGlyph = 0xE7A6, PenGlyph = 0xE76D, HighlighterGlyph = 0xE7E6,
            EraserGlyph = 0xE75C, WholeGlyph = 0xED62;
        constexpr Color Dark{ 0xFF, 0x20, 0x20, 0x20 };
        constexpr std::array<float, 9> Radii{ 1, 2, 3, 4, 6, 8, 12, 16, 20 };

        class Bar final : public Toolbar
        {
        public:
            Bar(HWND parent, WritingSurface& surface, ToolChoices choices, ToolbarTexts texts,
                std::function<UINT(Tool, PenPreset const&)> cursorFor, std::vector<ToolbarButton> extensions);
            ~Bar() override;

            HWND Window() const override { return container; }
            int Height() const override;
            ToolChoices const& Choices() const override { return choices; }
            void Choose(ToolChoices next) override
            {
                choices = std::move(next);
                Build();
                Apply(true);
            }
            void EnableButton(int id, bool enabled) override;

            static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wp, LPARAM lp);

        private:
            LRESULT Handle(UINT message, WPARAM wp, LPARAM lp);
            void Build();
            void Click(int id);
            void Menu(int id);
            void PresetMenu(Tool tool, int index);
            void RadiusMenu();
            void Apply(bool changed);
            void UpdateStates();
            std::wstring Tip(int id) const;
            RECT ButtonRect(int id) const;

            WritingSurface& surface;
            ToolChoices choices;
            ToolbarTexts texts;
            std::function<UINT(Tool, PenPreset const&)> cursorFor;
            std::vector<ToolbarButton> extensions;
            HWND container = nullptr, bar = nullptr;
            HIMAGELIST images = nullptr;
            int iconSize = 24;
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

            // Undo/redo follow the history (chained after the app's own handler).
            auto previous = std::move(surface.historyChanged);
            surface.historyChanged = [this, previous] {
                UpdateStates();
                if (previous) previous();
            };
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
            UINT dpi = GetDpiForWindow(container);
            iconSize = MulDiv(16, dpi, 96); // normal mouse size (the pen operates it)
            if (images) ImageList_Destroy(images);
            images = ImageList_Create(iconSize, iconSize, ILC_COLOR32, 16, 4);
            auto add = [this](HBITMAP bitmap) {
                int index = ImageList_Add(images, bitmap, nullptr);
                DeleteObject(bitmap);
                return index;
            };
            while (SendMessageW(bar, TB_BUTTONCOUNT, 0, 0) > 0) SendMessageW(bar, TB_DELETEBUTTON, 0, 0);
            SendMessageW(bar, TB_SETIMAGELIST, 0, reinterpret_cast<LPARAM>(images));

            std::vector<TBBUTTON> buttons;
            auto button = [&](int id, int image, BYTE style) {
                TBBUTTON b{};
                b.iBitmap = image;
                b.idCommand = id;
                b.fsState = TBSTATE_ENABLED;
                b.fsStyle = style;
                buttons.push_back(b);
            };
            auto separator = [&] {
                TBBUTTON b{};
                b.fsStyle = BTNS_SEP;
                buttons.push_back(b);
            };
            button(UndoId, add(GlyphIcon(UndoGlyph, Dark, std::nullopt, iconSize)), BTNS_BUTTON);
            button(RedoId, add(GlyphIcon(RedoGlyph, Dark, std::nullopt, iconSize)), BTNS_BUTTON);
            separator();
            auto pens = choices.PensOrDefault();
            for (int i = 0; i < static_cast<int>(pens.size()); ++i)
            {
                Color c = Palette::Pens[pens[i].color];
                button(PenId + i, add(GlyphIcon(PenGlyph, Dark, c, iconSize)), BTNS_CHECK | BTNS_DROPDOWN);
            }
            separator();
            auto highlighters = choices.HighlightersOrDefault();
            for (int i = 0; i < static_cast<int>(highlighters.size()); ++i)
            {
                Color c = Palette::Highlighters[highlighters[i].color];
                button(HighlighterId + i, add(GlyphIcon(HighlighterGlyph, Dark, c, iconSize)), BTNS_CHECK | BTNS_DROPDOWN);
            }
            separator();
            button(EraserId, add(GlyphIcon(EraserGlyph, Dark, std::nullopt, iconSize)), BTNS_CHECK);
            button(WholeId, add(GlyphIcon(WholeGlyph, Dark, std::nullopt, iconSize)), BTNS_CHECK | BTNS_DROPDOWN);
            // (Phase 2b/2c: selection, shapes, ruler.)
            if (!extensions.empty()) separator();
            for (size_t i = 0; i < extensions.size(); ++i)
            {
                button(ExtensionId + static_cast<int>(i), add(GlyphIcon(extensions[i].glyph, Dark, std::nullopt, iconSize)),
                    BTNS_BUTTON);
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
                if (extensions[i].id == id) SendMessageW(bar, TB_ENABLEBUTTON, ExtensionId + i, enabled);
            }
        }

        void Bar::UpdateStates()
        {
            SendMessageW(bar, TB_ENABLEBUTTON, UndoId, surface.CanUndo());
            SendMessageW(bar, TB_ENABLEBUTTON, RedoId, surface.CanRedo());
            for (int i = 0; i < static_cast<int>(choices.PensOrDefault().size()); ++i)
            {
                SendMessageW(bar, TB_CHECKBUTTON, PenId + i, choices.tool == Tool::Pen && choices.preset == i);
            }
            for (int i = 0; i < static_cast<int>(choices.HighlightersOrDefault().size()); ++i)
            {
                SendMessageW(bar, TB_CHECKBUTTON, HighlighterId + i, choices.tool == Tool::Highlighter && choices.preset == i);
            }
            SendMessageW(bar, TB_CHECKBUTTON, EraserId, choices.tool == Tool::Eraser);
            SendMessageW(bar, TB_CHECKBUTTON, WholeId, choices.wholeStrokeErasing);
        }

        // The surface gets the tool the choices describe; `changed`: tell the app.
        void Bar::Apply(bool changed)
        {
            ToolState state;
            state.tool = choices.tool;
            state.wholeStrokeErasing = choices.wholeStrokeErasing;
            state.eraserRadius = choices.eraserRadius;
            PenPreset preset{};
            if (choices.tool != Tool::Eraser)
            {
                auto presets = choices.tool == Tool::Pen ? choices.PensOrDefault() : choices.HighlightersOrDefault();
                choices.preset = std::clamp(choices.preset, 0, static_cast<int>(presets.size()) - 1);
                preset = presets[choices.preset];
                state.color = choices.tool == Tool::Pen ? Palette::Pens[preset.color] : Palette::Highlighters[preset.color];
                state.width = preset.width;
            }
            state.cursor = cursorFor ? cursorFor(choices.tool, preset) : 0;
            surface.SetTool(state);
            UpdateStates();
            Log(std::format("toolbar: {}", choices.Serialize()));
            if (changed && choicesChanged) choicesChanged(choices);
        }

        void Bar::Click(int id)
        {
            if (id == UndoId) surface.Undo();
            else if (id == RedoId) surface.Redo();
            else if (id >= PenId && id < PenId + 20)
            {
                if (choices.tool == Tool::Pen && choices.preset == id - PenId) return Menu(id); // a second tap: options
                choices.tool = Tool::Pen;
                choices.preset = id - PenId;
                Apply(true);
            }
            else if (id >= HighlighterId && id < HighlighterId + 20)
            {
                if (choices.tool == Tool::Highlighter && choices.preset == id - HighlighterId) return Menu(id);
                choices.tool = Tool::Highlighter;
                choices.preset = id - HighlighterId;
                Apply(true);
            }
            else if (id == EraserId)
            {
                choices.tool = Tool::Eraser;
                Apply(true);
            }
            else if (id == WholeId)
            {
                choices.wholeStrokeErasing = !choices.wholeStrokeErasing; // a toggle: the tool stays
                Apply(true);
            }
            else if (id >= ExtensionId && id < ExtensionId + static_cast<int>(extensions.size()))
            {
                if (auto& click = extensions[id - ExtensionId].click) click();
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
            if (id >= PenId && id < PenId + 20) PresetMenu(Tool::Pen, id - PenId);
            else if (id >= HighlighterId && id < HighlighterId + 20) PresetMenu(Tool::Highlighter, id - HighlighterId);
            else if (id == WholeId) RadiusMenu();
        }

        // A preset's options: its color (the palette) and width - a native popup menu below its button.
        void Bar::PresetMenu(Tool tool, int index)
        {
            bool pen = tool == Tool::Pen;
            auto presets = pen ? choices.PensOrDefault() : choices.HighlightersOrDefault();
            PenPreset& preset = presets[index];
            HMENU menu = CreatePopupMenu();
            std::vector<HBITMAP> swatches;
            int swatchSize = GetSystemMetricsForDpi(SM_CYMENUCHECK, GetDpiForWindow(container));
            size_t colors = pen ? Palette::Pens.size() : Palette::Highlighters.size();
            for (size_t i = 0; i < colors; ++i)
            {
                std::wstring name = pen ? texts.penColors[i] : texts.highlighterColors[i];
                AppendMenuW(menu, MF_STRING | (static_cast<int>(i) == preset.color ? MF_CHECKED : 0), 1 + i, name.c_str());
                HBITMAP swatch = Swatch(pen ? Palette::Pens[i] : Palette::Highlighters[i], swatchSize);
                swatches.push_back(swatch);
                MENUITEMINFOW info{ sizeof(info) };
                info.fMask = MIIM_BITMAP | MIIM_STATE;
                info.hbmpItem = swatch;
                // The swatch takes the check mark's place: the current color is shown in bold.
                info.fState = static_cast<int>(i) == preset.color ? MFS_CHECKED | MFS_DEFAULT : 0;
                SetMenuItemInfoW(menu, static_cast<UINT>(1 + i), FALSE, &info);
            }
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            std::vector<float> widths = pen ? std::vector<float>(PenWidths::Steps.begin(), PenWidths::Steps.end())
                                            : std::vector<float>(HighlighterWidths::Steps.begin(), HighlighterWidths::Steps.end());
            int current = pen ? PenWidths::StepOf(preset.width) : HighlighterWidths::StepOf(preset.width);
            for (size_t i = 0; i < widths.size(); ++i)
            {
                std::wstring label = std::vformat(texts.widthFormat, std::make_wformat_args(widths[i]));
                AppendMenuW(menu, MF_STRING | (static_cast<int>(i) == current ? MF_CHECKED : 0), 100 + i, label.c_str());
            }
            RECT r = ButtonRect((pen ? PenId : HighlighterId) + index);
            UINT chosen = TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, r.left, r.bottom, container, nullptr);
            DestroyMenu(menu);
            for (HBITMAP b : swatches) DeleteObject(b);
            if (chosen == 0) return;
            if (chosen < 100) preset.color = static_cast<int>(chosen - 1);
            else preset.width = widths[chosen - 100];
            (pen ? choices.pens : choices.highlighters) = presets;
            choices.tool = tool;
            choices.preset = index;
            Build(); // the preset's icon shows its color
            Apply(true);
        }

        // The exact eraser's circle size (page points).
        void Bar::RadiusMenu()
        {
            HMENU menu = CreatePopupMenu();
            for (size_t i = 0; i < Radii.size(); ++i)
            {
                std::wstring label = std::vformat(texts.widthFormat, std::make_wformat_args(Radii[i]));
                AppendMenuW(menu, MF_STRING | (Radii[i] == choices.eraserRadius ? MF_CHECKED : 0), 1 + i, label.c_str());
            }
            RECT r = ButtonRect(WholeId);
            UINT chosen = TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, r.left, r.bottom, container, nullptr);
            DestroyMenu(menu);
            if (chosen == 0) return;
            choices.eraserRadius = Radii[chosen - 1];
            Apply(true);
        }

        std::wstring Bar::Tip(int id) const
        {
            auto presetTip = [&](std::wstring const& kind, std::wstring const& color) {
                return std::format(L"{} – {} ({})", kind, color, texts.presetTip);
            };
            if (id == UndoId) return texts.undo;
            if (id == RedoId) return texts.redo;
            if (id >= PenId && id < PenId + 20)
            {
                auto pens = choices.PensOrDefault();
                return presetTip(texts.pen, texts.penColors[pens[id - PenId].color]);
            }
            if (id >= HighlighterId && id < HighlighterId + 20)
            {
                auto hl = choices.HighlightersOrDefault();
                return presetTip(texts.highlighter, texts.highlighterColors[hl[id - HighlighterId].color]);
            }
            if (id == EraserId) return texts.eraserTip;
            if (id == WholeId) return texts.wholeStrokeEraseTip;
            if (id >= ExtensionId && id < ExtensionId + static_cast<int>(extensions.size())) return extensions[id - ExtensionId].tip;
            return {};
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
                    std::wstring text = Tip(tip->iItem);
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
