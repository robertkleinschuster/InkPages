#include "toolbar/ToolbarMenus.h"

#include <format>
#include <memory>
#include <vector>
#include <winrt/base.h>

#include "inkpages/Log.h"
#include "inkpages/model/PaperAppearance.h"
#include "toolbar/Icons.h"
#include "toolbar/ToolbarModel.h"

namespace inkpages::detail
{
    namespace
    {
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

        struct BrushDeleter
        {
            void operator()(HBRUSH brush) const { DeleteObject(brush); }
        };

        // Returns the chosen item's index, or -1.
        int Show(HWND owner, RECT anchor, std::vector<Item> const& items, std::optional<ToolbarColors> const& colors)
        {
            std::unique_ptr<std::remove_pointer_t<HBRUSH>, BrushDeleter> background(
                colors ? CreateSolidBrush(colors->background) : nullptr);
            if (colors && !background) winrt::throw_last_error();
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
            if (colors)
            {
                MENUINFO info{ sizeof(info), MIM_BACKGROUND };
                info.hbrBack = background.get();
                winrt::check_bool(SetMenuInfo(menu, &info));
            }
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
                if (colors)
                {
                    MENUITEMINFOW info{ sizeof(info), MIIM_FTYPE | MIIM_DATA };
                    info.fType = MFT_OWNERDRAW;
                    info.dwItemData = reinterpret_cast<ULONG_PTR>(&item);
                    winrt::check_bool(SetMenuItemInfoW(menu, static_cast<UINT>(i + 1), FALSE, &info));
                }
                else if (item.swatch)
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

    bool MeasureToolMenu(HWND owner, MEASUREITEMSTRUCT& item)
    {
        if (item.CtlType != ODT_MENU || !item.itemData) return false;
        auto const& entry = *reinterpret_cast<Item const*>(item.itemData);
        UINT dpi = GetDpiForWindow(owner);
        NONCLIENTMETRICSW metrics{ sizeof(metrics) };
        winrt::check_bool(SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0, dpi));
        HFONT font = CreateFontIndirectW(&metrics.lfMenuFont);
        HDC dc = GetDC(owner);
        if (!font || !dc)
        {
            if (dc) ReleaseDC(owner, dc);
            if (font) DeleteObject(font);
            winrt::throw_hresult(E_FAIL);
        }
        auto old = SelectObject(dc, font);
        RECT text{};
        DrawTextW(dc, entry.text.c_str(), -1, &text, DT_CALCRECT | DT_SINGLELINE);
        SelectObject(dc, old);
        ReleaseDC(owner, dc);
        DeleteObject(font);
        item.itemWidth = text.right + MulDiv(42, dpi, 96);
        item.itemHeight = GetSystemMetricsForDpi(SM_CYMENU, dpi);
        return true;
    }

    bool DrawToolMenu(HWND owner, DRAWITEMSTRUCT const& item, ToolbarColors const& colors)
    {
        if (item.CtlType != ODT_MENU || !item.itemData) return false;
        auto const& entry = *reinterpret_cast<Item const*>(item.itemData);
        auto dc = item.hDC;
        int saved = SaveDC(dc);
        if (!saved) winrt::throw_hresult(E_FAIL);
        auto brush = static_cast<HBRUSH>(GetStockObject(DC_BRUSH));
        SetDCBrushColor(dc, item.itemState & ODS_SELECTED ? colors.highlight : colors.background);
        FillRect(dc, &item.rcItem, brush);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, colors.text);

        UINT dpi = GetDpiForWindow(owner);
        RECT label = item.rcItem;
        label.left += MulDiv(42, dpi, 96);
        label.right -= MulDiv(6, dpi, 96);
        DrawTextW(dc, entry.text.c_str(), -1, &label, DT_SINGLELINE | DT_VCENTER |
            ((item.itemState & ODS_NOACCEL) ? DT_HIDEPREFIX : 0));

        RECT mark = item.rcItem;
        mark.right = mark.left + MulDiv(21, dpi, 96);
        if (entry.checked) DrawTextW(dc, L"\u2713", -1, &mark, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        if (entry.swatch)
        {
            int size = MulDiv(13, dpi, 96);
            int top = (mark.top + mark.bottom - size) / 2;
            RECT sample{ mark.right, top, mark.right + size, top + size };
            auto rgb = *entry.swatch;
            SetDCBrushColor(dc, RGB(rgb.r, rgb.g, rgb.b));
            FillRect(dc, &sample, brush);
            SetDCBrushColor(dc, colors.text);
            FrameRect(dc, &sample, brush);
        }
        if (!RestoreDC(dc, saved)) LogError("RestoreDC (toolbar menu)");
        return true;
    }

    std::optional<ToolChoices> PresetMenu(HWND owner, RECT anchor, ToolChoices c, Tool tool, int index, ToolbarTexts const& t,
        std::optional<ToolbarColors> const& colors, bool darkPaper)
    {
        bool pen = tool == Tool::Pen;
        auto presets = pen ? c.PensOrDefault() : c.HighlightersOrDefault();
        PenPreset& preset = presets[index];
        std::vector<Item> items;
        size_t colorCount = pen ? Palette::Pens.size() : Palette::Highlighters.size();
        for (size_t i = 0; i < colorCount; ++i)
        {
            auto color = pen ? Palette::Pens[i] : Palette::Highlighters[i];
            if (darkPaper) color = PaperAppearance::DarkToolInk(color, !pen);
            items.push_back({ pen ? t.penColors[i] : t.highlighterColors[i], static_cast<int>(i) == preset.color, color });
        }
        items.push_back({ {}, false, std::nullopt, true });
        std::vector<float> widths = pen ? std::vector<float>(PenWidths::Steps.begin(), PenWidths::Steps.end())
                                        : std::vector<float>(HighlighterWidths::Steps.begin(), HighlighterWidths::Steps.end());
        for (size_t i = 0; i < widths.size(); ++i) items.push_back({ t.sizeSteps[i], widths[i] == preset.width });
        int chosen = Show(owner, anchor, items, colors);
        if (chosen < 0) return std::nullopt;
        if (chosen < static_cast<int>(colorCount)) preset.color = chosen;
        else preset.width = widths[chosen - colorCount - 1];
        (pen ? c.pens : c.highlighters) = presets;
        c.tool = tool;
        c.preset = index;
        return c;
    }

    std::optional<ToolChoices> RadiusMenu(HWND owner, RECT anchor, ToolChoices c, ToolbarTexts const& t, std::optional<ToolbarColors> const& colors)
    {
        std::vector<Item> items;
        int current = EraserSizes::StepOf(c.eraserRadius, c.squareEraser);
        for (size_t i = 0; i < t.sizeSteps.size(); ++i)
            items.push_back({ t.sizeSteps[i],
                EraserSizes::Extent(EraserSizes::SquareHalfSides[i], c.squareEraser) == c.eraserRadius });
        items.push_back({ {}, false, std::nullopt, true });
        items.push_back({ t.squareEraser, c.squareEraser });
        int chosen = Show(owner, anchor, items, colors);
        if (chosen < 0) return std::nullopt;
        if (chosen == static_cast<int>(items.size()) - 1)
        {
            float extent = EraserSizes::Extent(EraserSizes::SquareHalfSides[current], c.squareEraser);
            bool named = extent == c.eraserRadius;
            c.squareEraser = !c.squareEraser;
            c.eraserRadius = named ? EraserSizes::Extent(EraserSizes::SquareHalfSides[current], c.squareEraser)
                                  : c.eraserRadius * (c.squareEraser ? 1.f / EraserSizes::CircleFactor : EraserSizes::CircleFactor);
        }
        else c.eraserRadius = EraserSizes::Extent(EraserSizes::SquareHalfSides[chosen], c.squareEraser);
        return c;
    }

    std::optional<ToolChoices> SelectModeMenu(HWND owner, RECT anchor, ToolChoices c, ToolbarTexts const& t, std::optional<ToolbarColors> const& colors)
    {
        int chosen = Show(owner, anchor, { { t.lasso, c.selectMode == SelectMode::Lasso }, { t.rectangle, c.selectMode == SelectMode::Rectangle } }, colors);
        if (chosen < 0) return std::nullopt;
        c.selectMode = static_cast<SelectMode>(chosen);
        c.tool = Tool::Select;
        return c;
    }

    std::optional<ToolChoices> ShapeMenu(HWND owner, RECT anchor, ToolChoices c, ToolbarTexts const& t, std::optional<ToolbarColors> const& colors)
    {
        std::vector<Item> items;
        for (int i = 0; i < 4; ++i) items.push_back({ t.shapeKinds[i], c.DrawingShape() && c.shape == static_cast<ShapeKind>(i) });
        items.push_back({ {}, false, std::nullopt, true });
        items.push_back({ t.recognizeShapes, c.recognizeShapes });
        int chosen = Show(owner, anchor, items, colors);
        if (chosen < 0) return std::nullopt;
        if (chosen < 4)
            c.ToggleShape(static_cast<ShapeKind>(chosen));
        else c.recognizeShapes = !c.recognizeShapes; // a setting of the pen: the tool stays
        return c;
    }

    std::optional<ToolChoices> CopyAreaMenu(HWND owner, RECT anchor, ToolChoices c, ToolbarTexts const& t,
        std::optional<ToolbarColors> const& colors)
    {
        int chosen = Show(owner, anchor, { { t.copyAreaTransparent, !c.copyAreaBackground },
            { t.copyAreaWithBackground, c.copyAreaBackground }, { {}, false, std::nullopt, true },
            { t.copyAreaFrame, c.copyAreaFrame }, { t.copyAreaObjects, c.copyAreaObjects } }, colors);
        if (chosen < 0) return std::nullopt;
        if (chosen == 3) c.copyAreaFrame = !c.copyAreaFrame;
        else if (chosen == 4) c.copyAreaObjects = !c.copyAreaObjects;
        else c.copyAreaBackground = chosen == 1;
        c.tool = Tool::CopyArea;
        return c;
    }

    std::optional<ToolChoices> LaserMenu(HWND owner, RECT anchor, ToolChoices c, std::optional<ToolbarColors> const& colors)
    {
        std::vector<Item> items;
        for (int seconds : ToolChoices::LaserDurations)
            items.push_back({ std::to_wstring(seconds) + L" s", seconds == c.laserSeconds });
        int chosen = Show(owner, anchor, items, colors);
        if (chosen < 0) return std::nullopt;
        c.laserSeconds = ToolChoices::LaserDurations[chosen];
        c.tool = Tool::Laser;
        return c;
    }

    std::optional<ToolChoices> TextMenu(HWND owner, RECT anchor, ToolChoices c, ToolbarTexts const& t,
        std::optional<ToolbarColors> const& colors, bool darkPaper)
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
            auto color = Palette::Pens[i];
            if (darkPaper) color = PaperAppearance::DarkToolInk(color, false);
            items.push_back({ t.penColors[i], static_cast<int>(i) == c.textColor, color });
        }
        int chosen = Show(owner, anchor, items, colors);
        if (chosen < 0) return std::nullopt;
        if (chosen < fonts) c.textFont = chosen;
        else if (chosen < fonts + 1 + sizes) c.textSize = TextSizes::Steps[chosen - fonts - 1];
        else c.textColor = chosen - fonts - sizes - 2;
        c.tool = Tool::Text;
        return c;
    }

    std::optional<StencilChoice> StencilMenu(HWND owner, RECT anchor, StencilChoice now, ToolbarTexts const& t, std::optional<ToolbarColors> const& colors)
    {
        int chosen = Show(owner, anchor, { { t.ruler, now.ruler }, { t.protractor, now.protractor } }, colors);
        if (chosen < 0) return std::nullopt;
        if (chosen == 0) now.ruler = !now.ruler;
        else now.protractor = !now.protractor;
        return now;
    }
}
