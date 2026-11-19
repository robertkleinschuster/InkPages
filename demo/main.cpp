// InkPages Demo: the library's development and test app (UI tests in Windows Sandbox run against it).
// Phase 0: the window (comctl32 v6 controls with visual styles, PerMonitorV2 from the manifest) and the log; the
// writing area follows in phase 1 (see docs in xournal-studio-windows: docs/CPP-UMZUG.md).
// Log: %INKPAGES_DEMO_LOG%, otherwise nowhere.

#include <windows.h>
#include <commctrl.h>

#include <winrt/base.h>

#include <format>
#include <string>

#include "inkpages/Log.h"

namespace
{
    LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wp, LPARAM lp)
    {
        switch (message)
        {
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(window, message, wp, lp);
        }
    }

    std::wstring Environment(wchar_t const* name)
    {
        wchar_t value[MAX_PATH]{};
        DWORD length = GetEnvironmentVariableW(name, value, MAX_PATH);
        return length > 0 && length < MAX_PATH ? std::wstring(value, length) : std::wstring();
    }
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show)
{
    inkpages::SetLogFile(Environment(L"INKPAGES_DEMO_LOG"));
    inkpages::Log("demo: start");
    try
    {
        winrt::init_apartment(winrt::apartment_type::single_threaded);

        INITCOMMONCONTROLSEX controls{ sizeof(controls), ICC_WIN95_CLASSES };
        if (!InitCommonControlsEx(&controls))
        {
            inkpages::LogError("InitCommonControlsEx failed");
        }

        WNDCLASSEXW windowClass{ sizeof(windowClass) };
        windowClass.lpfnWndProc = WindowProc;
        windowClass.hInstance = instance;
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        windowClass.lpszClassName = L"InkPagesDemo";
        winrt::check_bool(RegisterClassExW(&windowClass));

        HWND window = CreateWindowExW(0, windowClass.lpszClassName, L"InkPages Demo", WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, nullptr, nullptr, instance, nullptr);
        winrt::check_pointer(window);
        ShowWindow(window, show == SW_SHOWDEFAULT ? SW_SHOWMAXIMIZED : show);
        inkpages::Log(std::format("demo: window shown, DPI {}", GetDpiForWindow(window)));

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
