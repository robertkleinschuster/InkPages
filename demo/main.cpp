// InkPages Demo: the library's development and test app (UI tests in Windows Sandbox run against it). It uses only the
// library's public API: a writing surface with A4 pages on graph paper, a status bar, and keys for the pen and the view.
//   1/2/3 black/blue/red   S/L small/large width   Q/W/E/T zoom 0.5/1/2/4   F fit width
//   Page Down/Up next/previous page   Home first page
// Environment: INKPAGES_DEMO_LOG (log file), INKPAGES_DEMO_PAGES (page count, default 4),
//              INKPAGES_DEMO_MOUSE=1 (the mouse writes too).

#include <windows.h>
#include <commctrl.h>

#include <winrt/base.h>
#include <winrt/Windows.Foundation.Collections.h>

#include <algorithm>
#include <format>
#include <map>
#include <memory>
#include <string>

#include "inkpages/Log.h"
#include "inkpages/WritingSurface.h"
#include "resource.h"

namespace
{
    using inkpages::Color;
    using Stroke = inkpages::WritingSurface::InkStroke;

    struct Pen
    {
        char const* name;
        Color color;
        UINT smallCursor, largeCursor;
    };
    Pen const Pens[] = {
        { "black", { 0xFF, 0x00, 0x00, 0x00 }, IDC_DOT_BLACK_SMALL, IDC_DOT_BLACK_LARGE },
        { "blue", { 0xFF, 0x1E, 0x5A, 0xC8 }, IDC_DOT_BLUE_SMALL, IDC_DOT_BLUE_LARGE },
        { "red", { 0xFF, 0xD2, 0x1E, 0x1E }, IDC_DOT_RED_SMALL, IDC_DOT_RED_LARGE },
    };
    float const Widths[] = { 2, 6 }; // DIPs at half pressure (the cursors' dot diameters, demo/tools/make_cursors.py)

    std::unique_ptr<inkpages::WritingSurface> surface;
    HWND status = nullptr;
    int pen = 0, width = 0;

    // Lazy pages: the strokes of pages far from the view (page coordinates); used on the ink thread only.
    std::map<int, std::vector<Stroke>> parked;

    std::wstring Environment(wchar_t const* name)
    {
        wchar_t value[MAX_PATH]{};
        DWORD length = GetEnvironmentVariableW(name, value, MAX_PATH);
        return length > 0 && length < MAX_PATH ? std::wstring(value, length) : std::wstring();
    }

    void UpdateStatus()
    {
        if (!status || !surface) return;
        auto page = surface->PageInView();
        std::wstring text = std::format(L"Pen {} {:.0f} DIP   Zoom {:.0f} %   Page {} / {}   "
            L"(1/2/3 color, S/L width, Q/W/E/T zoom, F fit width, PgUp/PgDn pages)",
            winrt::to_hstring(Pens[pen].name).c_str(), Widths[width], surface->CurrentView().zoom * 100,
            page ? *page + 1 : 0, surface->Layout().Count());
        SendMessageW(status, SB_SETTEXTW, 0, reinterpret_cast<LPARAM>(text.c_str()));
    }

    void ApplyPen()
    {
        surface->SetPen(Pens[pen].color, Widths[width], width == 0 ? Pens[pen].smallCursor : Pens[pen].largeCursor);
        inkpages::Log(std::format("demo: pen {} {}", Pens[pen].name, Widths[width]));
        UpdateStatus();
    }

    void Layout(HWND window)
    {
        RECT rc;
        GetClientRect(window, &rc);
        SendMessageW(status, WM_SIZE, 0, 0);
        RECT sr;
        GetWindowRect(status, &sr);
        if (surface) MoveWindow(surface->Window(), 0, 0, rc.right, rc.bottom - (sr.bottom - sr.top), TRUE);
    }

    void OnKey(WPARAM key)
    {
        int page = surface->PageInView().value_or(0);
        switch (key)
        {
        case '1': pen = 0; ApplyPen(); break;
        case '2': pen = 1; ApplyPen(); break;
        case '3': pen = 2; ApplyPen(); break;
        case 'S': width = 0; ApplyPen(); break;
        case 'L': width = 1; ApplyPen(); break;
        case 'Q': surface->ZoomTo(0.5f); break;
        case 'W': surface->ZoomTo(1); break;
        case 'E': surface->ZoomTo(2); break;
        case 'T': surface->ZoomTo(4); break;
        case 'F': surface->FitWidth(); break;
        case VK_NEXT: surface->ScrollToPage(page + 1); break;
        case VK_PRIOR: surface->ScrollToPage(page - 1); break;
        case VK_HOME: surface->ScrollToPage(0); break;
        default: return;
        }
        inkpages::Log(std::format("demo: key 0x{:X}", key));
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
                if (surface) OnKey(wp);
                return 0;
            case WM_DESTROY:
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

        int count = 4;
        if (auto value = Environment(L"INKPAGES_DEMO_PAGES"); !value.empty()) count = std::max(1, _wtoi(value.c_str()));
        inkpages::PageSpec a4;
        a4.paper.kind = inkpages::PaperKind::Graph;
        a4.paper.spacing = 5 * 96 / 25.4f; // 5 mm squares
        surface->SetPages(std::vector<inkpages::PageSpec>(count, a4));
        ApplyPen();

        ShowWindow(window, show == SW_SHOWDEFAULT ? SW_SHOWMAXIMIZED : show);
        Layout(window);
        surface->FitWidth();
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
