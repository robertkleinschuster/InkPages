#include "toolbar/ToolbarMenus.h"

#include <format>
#include <memory>
#include <vector>

#include "inkpages/Log.h"
#include "toolbar/Icons.h"
#include "toolbar/ToolbarModel.h"

namespace inkpages::detail
{
    namespace
    {
        constexpr std::array<float, 9> Radii{ 1, 2, 3, 4, 6, 8, 12, 16, 20 };

        struct Item
        {
            std::wstring text;
            bool checked = false;
            std::optional<Color> swatch;
            bool separator = false;
        };

        struct BitmapDeleter
        {
            void operator()(HBITMAP b) const { DeleteObject(b); }
        };

        // Returns the chosen item's index, or -1.
        int Show(HWND owner, RECT anchor, std::vector<Item> const& items)
        {
            HMENU menu = CreatePopupMenu();
            if (!menu)
            {
                LogHr("CreatePopupMenu", HRESULT_FROM_WIN32(GetLastError()));
                return -1;
            }
            std::vector<std::unique_ptr<std::remove_pointer_t<HBITMAP>, BitmapDeleter>> swatches;
            struct OwnedMenu
            {
                HMENU handle;
                ~OwnedMenu() { if (!DestroyMenu(handle)) LogError("DestroyMenu (toolbar) failed"); }
            } owned{ menu };
            swatches.reserve(items.size());
            int swatchSize = GetSystemMetricsForDpi(SM_CYMENUCHECK, GetDpiForWindow(owner));
            for (size_t i = 0; i < items.size(); ++i)
            {
                Item const& item = items[i];
                if (item.separator)
                {
                    if (!AppendMenuW(menu, MF_SEPARATOR, 0, nullptr))
                    {
                        LogHr("AppendMenuW (toolbar separator)", HRESULT_FROM_WIN32(GetLastError()));
                        return -1;
                    }
                    continue;
                }
                if (!AppendMenuW(menu, MF_STRING | (item.checked ? MF_CHECKED : 0), i + 1, item.text.c_str()))
                {
                    LogHr("AppendMenuW (toolbar item)", HRESULT_FROM_WIN32(GetLastError()));
                    return -1;
                }
                if (item.swatch)
                {
                    swatches.emplace_back(Swatch(*item.swatch, swatchSize));
                    if (!swatches.back())
                    {
                        LogError("CreateDIBSection (toolbar swatch) failed");
                        continue;
                    }
                    MENUITEMINFOW info{ sizeof(info) };
                    info.fMask = MIIM_BITMAP | MIIM_STATE;
                    info.hbmpItem = swatches.back().get();
                    // The swatch takes the check mark's place: the current color is shown in bold.
                    info.fState = item.checked ? MFS_CHECKED | MFS_DEFAULT : 0;
                    if (!SetMenuItemInfoW(menu, static_cast<UINT>(i + 1), FALSE, &info))
                        LogHr("SetMenuItemInfoW (toolbar swatch)", HRESULT_FROM_WIN32(GetLastError()));
                }
            }
            UINT chosen = TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, anchor.left, anchor.bottom, owner, nullptr);
            return static_cast<int>(chosen) - 1;
        }

        std::wstring Size(ToolbarTexts const& t, float value) { return FormatSize(t, value); }
    }

    std::optional<ToolChoices> PresetMenu(HWND owner, RECT anchor, ToolChoices c, Tool tool, int index, ToolbarTexts const& t)
    {
        bool pen = tool == Tool::Pen;
        auto presets = pen ? c.PensOrDefault() : c.HighlightersOrDefault();
        PenPreset& preset = presets[index];
        std::vector<Item> items;
        size_t colors = pen ? Palette::Pens.size() : Palette::Highlighters.size();
        for (size_t i = 0; i < colors; ++i)
        {
            items.push_back({ pen ? t.penColors[i] : t.highlighterColors[i], static_cast<int>(i) == preset.color,
                pen ? Palette::Pens[i] : Palette::Highlighters[i] });
        }
        items.push_back({ {}, false, std::nullopt, true });
        std::vector<float> widths = pen ? std::vector<float>(PenWidths::Steps.begin(), PenWidths::Steps.end())
                                        : std::vector<float>(HighlighterWidths::Steps.begin(), HighlighterWidths::Steps.end());
        int current = pen ? PenWidths::StepOf(preset.width) : HighlighterWidths::StepOf(preset.width);
        for (size_t i = 0; i < widths.size(); ++i) items.push_back({ Size(t, widths[i]), static_cast<int>(i) == current });
        int chosen = Show(owner, anchor, items);
        if (chosen < 0) return std::nullopt;
        if (chosen < static_cast<int>(colors)) preset.color = chosen;
        else preset.width = widths[chosen - colors - 1];
        (pen ? c.pens : c.highlighters) = presets;
        c.tool = tool;
        c.preset = index;
        return c;
    }

    std::optional<ToolChoices> RadiusMenu(HWND owner, RECT anchor, ToolChoices c, ToolbarTexts const& t)
    {
        std::vector<Item> items;
        for (float r : Radii) items.push_back({ Size(t, r), r == c.eraserRadius });
        int chosen = Show(owner, anchor, items);
        if (chosen < 0) return std::nullopt;
        c.eraserRadius = Radii[chosen];
        return c;
    }

    std::optional<ToolChoices> SelectModeMenu(HWND owner, RECT anchor, ToolChoices c, ToolbarTexts const& t)
    {
        int chosen = Show(owner, anchor, { { t.lasso, c.selectMode == SelectMode::Lasso }, { t.rectangle, c.selectMode == SelectMode::Rectangle } });
        if (chosen < 0) return std::nullopt;
        c.selectMode = static_cast<SelectMode>(chosen);
        c.tool = Tool::Select;
        return c;
    }

    std::optional<ToolChoices> ShapeMenu(HWND owner, RECT anchor, ToolChoices c, ToolbarTexts const& t)
    {
        std::vector<Item> items;
        for (int i = 0; i < 4; ++i) items.push_back({ t.shapeKinds[i], c.DrawingShape() && c.shape == static_cast<ShapeKind>(i) });
        items.push_back({ {}, false, std::nullopt, true });
        items.push_back({ t.recognizeShapes, c.recognizeShapes });
        int chosen = Show(owner, anchor, items);
        if (chosen < 0) return std::nullopt;
        if (chosen < 4)
            c.ToggleShape(static_cast<ShapeKind>(chosen));
        else c.recognizeShapes = !c.recognizeShapes; // a setting of the pen: the tool stays
        return c;
    }

    std::optional<ToolChoices> TextMenu(HWND owner, RECT anchor, ToolChoices c, ToolbarTexts const& t)
    {
        std::vector<Item> items;
        int fonts = static_cast<int>(TextFonts::Faces.size()), sizes = static_cast<int>(TextSizes::Steps.size());
        for (int i = 0; i < fonts; ++i) items.push_back({ TextFonts::Faces[i], i == c.textFont });
        items.push_back({ {}, false, std::nullopt, true });
        int size = TextSizes::StepOf(c.textSize);
        for (int i = 0; i < sizes; ++i) items.push_back({ Size(t, TextSizes::Steps[i]), i == size });
        items.push_back({ {}, false, std::nullopt, true });
        for (size_t i = 0; i < Palette::Pens.size(); ++i)
        {
            items.push_back({ t.penColors[i], static_cast<int>(i) == c.textColor, Palette::Pens[i] });
        }
        int chosen = Show(owner, anchor, items);
        if (chosen < 0) return std::nullopt;
        if (chosen < fonts) c.textFont = chosen;
        else if (chosen < fonts + 1 + sizes) c.textSize = TextSizes::Steps[chosen - fonts - 1];
        else c.textColor = chosen - fonts - sizes - 2;
        c.tool = Tool::Text;
        return c;
    }

    std::optional<StencilChoice> StencilMenu(HWND owner, RECT anchor, StencilChoice now, ToolbarTexts const& t)
    {
        int chosen = Show(owner, anchor, { { t.ruler, now.ruler }, { t.protractor, now.protractor } });
        if (chosen < 0) return std::nullopt;
        if (chosen == 0) now.ruler = !now.ruler;
        else now.protractor = !now.protractor;
        return now;
    }
}
