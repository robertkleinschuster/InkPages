// InkPages Demo: the library's development and test app (UI tests in Windows Sandbox run against it). It uses only the
// library's public API: the toolbar (German texts, as an app passes its language), a writing surface with A4 pages on
// graph paper, a status bar.
// Keys: Ctrl+Z / Ctrl+Y undo/redo; tools (test hooks): 1/2/3 pens, 4/5 highlighters, 0 eraser, X erase whole strokes;
//       Q/W/E/T zoom 0.5/1/2/4, F fit width, Page Down/Up next/previous page, Home first page.
// Environment: INKPAGES_DEMO_LOG (log file), INKPAGES_DEMO_PAGES (page count, default 4),
//              INKPAGES_DEMO_MOUSE=1 (the mouse writes too), INKPAGES_DEMO_CHOICES (file keeping the tool choices).

#include <windows.h>
#include <commctrl.h>

#include <winrt/base.h>
#include <winrt/Windows.Foundation.Collections.h>

#include <algorithm>
#include <format>
#include <fstream>
#include <map>
#include <memory>
#include <string>

#include "inkpages/Log.h"
#include "inkpages/Toolbar.h"
#include "inkpages/WritingSurface.h"

namespace
{
    using Stroke = inkpages::WritingSurface::InkStroke;

    std::unique_ptr<inkpages::WritingSurface> surface;
    std::unique_ptr<inkpages::Toolbar> toolbar;
    HWND status = nullptr;
    std::wstring choicesFile;

    // Lazy pages: the strokes of pages far from the view (page coordinates); used on the ink thread only.
    std::map<int, std::vector<Stroke>> parked;

    std::wstring Environment(wchar_t const* name)
    {
        wchar_t value[MAX_PATH]{};
        DWORD length = GetEnvironmentVariableW(name, value, MAX_PATH);
        return length > 0 && length < MAX_PATH ? std::wstring(value, length) : std::wstring();
    }

    // The toolbar in German (what the Xournal Studio and MatheTrainer apps pass).
    inkpages::ToolbarTexts German()
    {
        inkpages::ToolbarTexts t;
        t.pen = L"Stift";
        t.highlighter = L"Textmarker";
        t.presetTip = L"nochmal tippen für Farbe und Stärke";
        t.eraser = L"Radierer";
        t.eraserTip = L"Radierer – die Radierer-Taste des Stifts radiert auch, mit jedem Werkzeug";
        t.wholeStrokeErase = L"Ganze Striche radieren";
        t.wholeStrokeEraseTip = L"Ganze Striche radieren: Radierer und Radierer-Taste des Stifts löschen jeden Strich, "
            L"den sie berühren. Aus: nur, was im Kreis liegt. Der Pfeil: Größe des Kreises";
        t.eraserRadius = L"Größe";
        t.undo = L"Rückgängig (Strg+Z)";
        t.redo = L"Wiederholen (Strg+Y)";
        t.color = L"Farbe";
        t.width = L"Stärke";
        t.widthFormat = L"{} pt";
        t.penColors = { L"Schwarz", L"Blau", L"Rot", L"Grün", L"Grau", L"Hellblau", L"Hellgrün", L"Magenta", L"Orange", L"Gelb" };
        t.highlighterColors = { L"Gelb", L"Grün", L"Hellblau", L"Magenta", L"Orange" };
        return t;
    }

    // The demo's cursors (demo/tools/make_cursors.py): a dot per palette color x width step.
    UINT CursorFor(inkpages::Tool tool, inkpages::PenPreset const& preset)
    {
        switch (tool)
        {
        case inkpages::Tool::Pen: return 1000 + preset.color * 10 + inkpages::PenWidths::StepOf(preset.width);
        case inkpages::Tool::Highlighter: return 2000 + preset.color * 10 + inkpages::HighlighterWidths::StepOf(preset.width);
        default: return 0; // the eraser: Windows' cursor (the exact eraser shows its circle)
        }
    }

    inkpages::ToolChoices LoadChoices()
    {
        if (choicesFile.empty()) return {};
        std::ifstream file(choicesFile);
        std::string line;
        std::getline(file, line);
        return inkpages::ToolChoices::Parse(line);
    }

    void SaveChoices(inkpages::ToolChoices const& choices)
    {
        if (choicesFile.empty()) return;
        std::ofstream file(choicesFile, std::ios::trunc);
        file << choices.Serialize() << '\n';
        if (!file) inkpages::LogError("demo: the tool choices could not be saved");
    }

    void UpdateStatus()
    {
        if (!status || !surface) return;
        auto page = surface->PageInView();
        std::wstring text = std::format(L"Zoom {:.0f} %   Seite {} / {}   {}", surface->CurrentView().zoom * 100,
            page ? *page + 1 : 0, surface->Layout().Count(), surface->AtSavePoint() ? L"" : L"geändert");
        SendMessageW(status, SB_SETTEXTW, 0, reinterpret_cast<LPARAM>(text.c_str()));
    }

    void Layout(HWND window)
    {
        RECT rc;
        GetClientRect(window, &rc);
        SendMessageW(status, WM_SIZE, 0, 0);
        RECT sr;
        GetWindowRect(status, &sr);
        int top = toolbar ? toolbar->Height() : 0;
        if (toolbar) MoveWindow(toolbar->Window(), 0, 0, rc.right, top, TRUE);
        if (surface) MoveWindow(surface->Window(), 0, top, rc.right, rc.bottom - top - (sr.bottom - sr.top), TRUE);
    }

    void ChooseTool(inkpages::Tool tool, int preset)
    {
        auto choices = toolbar->Choices();
        choices.tool = tool;
        choices.preset = preset;
        toolbar->Choose(choices);
    }

    void OnKey(WPARAM key)
    {
        bool ctrl = GetKeyState(VK_CONTROL) < 0;
        int page = surface->PageInView().value_or(0);
        if (ctrl && key == 'Z') surface->Undo();
        else if (ctrl && key == 'Y') surface->Redo();
        else if (ctrl) return;
        else if (key >= '1' && key <= '3') ChooseTool(inkpages::Tool::Pen, static_cast<int>(key - '1'));
        else if (key == '4' || key == '5') ChooseTool(inkpages::Tool::Highlighter, static_cast<int>(key - '4'));
        else if (key == '0') ChooseTool(inkpages::Tool::Eraser, 0);
        else if (key == 'X')
        {
            auto choices = toolbar->Choices();
            choices.wholeStrokeErasing = !choices.wholeStrokeErasing;
            toolbar->Choose(choices);
        }
        else if (key == 'Q') surface->ZoomTo(0.5f);
        else if (key == 'W') surface->ZoomTo(1);
        else if (key == 'E') surface->ZoomTo(2);
        else if (key == 'T') surface->ZoomTo(4);
        else if (key == 'F') surface->FitWidth();
        else if (key == VK_NEXT) surface->ScrollToPage(page + 1);
        else if (key == VK_PRIOR) surface->ScrollToPage(page - 1);
        else if (key == VK_HOME) surface->ScrollToPage(0);
        else return;
        inkpages::Log(std::format("demo: key 0x{:X}{}", key, ctrl ? " with Ctrl" : ""));
    }

    LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wp, LPARAM lp)
    {
        try
        {
            switch (message)
            {
            case WM_SIZE:
                Layout(window);
                return 0;
            case WM_KEYDOWN:
                if (surface && toolbar) OnKey(wp);
                return 0;
            case WM_DESTROY:
                toolbar.reset();
                surface.reset();
                PostQuitMessage(0);
                return 0;
            }
        }
        catch (...)
        {
            inkpages::LogException(std::format("demo: message 0x{:X}", message));
        }
        return DefWindowProcW(window, message, wp, lp);
    }
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show)
{
    inkpages::SetLogFile(Environment(L"INKPAGES_DEMO_LOG"));
    inkpages::Log("demo: start");
    try
    {
        winrt::init_apartment(winrt::apartment_type::single_threaded);

        INITCOMMONCONTROLSEX controls{ sizeof(controls), ICC_WIN95_CLASSES | ICC_BAR_CLASSES };
        if (!InitCommonControlsEx(&controls)) inkpages::LogError("InitCommonControlsEx failed");

        WNDCLASSEXW windowClass{ sizeof(windowClass) };
        windowClass.lpfnWndProc = WindowProc;
        windowClass.hInstance = instance;
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        windowClass.lpszClassName = L"InkPagesDemo";
        winrt::check_bool(RegisterClassExW(&windowClass));

        HWND window = CreateWindowExW(0, windowClass.lpszClassName, L"InkPages Demo", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
            CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, nullptr, nullptr, instance, nullptr);
        winrt::check_pointer(window);
        status = CreateWindowExW(0, STATUSCLASSNAMEW, L"", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window, nullptr, instance, nullptr);

        inkpages::SurfaceOptions options;
        options.mouseInks = Environment(L"INKPAGES_DEMO_MOUSE") == L"1";
        surface = inkpages::WritingSurface::Create(window, options);
        surface->viewChanged = UpdateStatus;
        surface->historyChanged = UpdateStatus; // before the toolbar: it chains this handler
        surface->load = [](int page) {
            std::vector<Stroke> strokes;
            if (auto it = parked.find(page); it != parked.end())
            {
                strokes = std::move(it->second);
                parked.erase(it);
            }
            return strokes;
        };
        surface->store = [](int page, std::vector<Stroke> strokes) {
            if (!strokes.empty()) parked[page] = std::move(strokes);
        };

        choicesFile = Environment(L"INKPAGES_DEMO_CHOICES");
        toolbar = inkpages::Toolbar::Create(window, *surface, LoadChoices(), German(), CursorFor);
        toolbar->choicesChanged = SaveChoices;

        int count = 4;
        if (auto value = Environment(L"INKPAGES_DEMO_PAGES"); !value.empty()) count = std::max(1, _wtoi(value.c_str()));
        inkpages::PageSpec a4;
        a4.paper.kind = inkpages::PaperKind::Graph;
        a4.paper.spacing = 5 * 72 / 25.4f; // 5 mm squares (page units are points)
        surface->SetPages(std::vector<inkpages::PageSpec>(count, a4));

        ShowWindow(window, show == SW_SHOWDEFAULT ? SW_SHOWMAXIMIZED : show);
        Layout(window);
        surface->FitWidth();
        UpdateStatus();
        inkpages::Log(std::format("demo: window shown, DPI {}, {} pages", GetDpiForWindow(window), count));

        MSG message;
        while (GetMessageW(&message, nullptr, 0, 0) > 0)
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        inkpages::Log("demo: end");
        return static_cast<int>(message.wParam);
    }
    catch (...)
    {
        inkpages::LogException("demo");
        return 1;
    }
}
