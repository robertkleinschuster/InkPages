// uiaprobe: what UI Automation (the native client, as Narrator uses it) sees in a window.
//   uiaprobe.exe <window class>            every element, depth first: control type id, name, automation id
//   uiaprobe.exe <window class> <text>     the first element whose name contains <text>: "x y" = its center in screen
//                                          pixels (exit code 1 when there is none) - UI tests click it there
//   uiaprobe.exe <window class> <text> --tree-item   match tree items only
//   uiaprobe.exe <window class> <text> --exact       match the full name
//   uiaprobe.exe --focus                   the focused element: control type, class, keyboard focus, text pattern and
//                                          its text, bounds (exit code 1 when it has no text pattern)
//   uiaprobe.exe --keyboard                the touch keyboard's windows (TextInputHost): class, title, visible, cloaked,
//                                          bounds
// (The managed System.Windows.Automation client of Windows PowerShell does not bridge comctl32 toolbars: it sees an
// empty pane, while the native client sees the toolbar and its buttons through their MSAA names.)

#include <windows.h>
#include <ole2.h>
#include <uiautomation.h>

#include <dwmapi.h>
#include <psapi.h>

#include <cstdio>
#include <string>
#include <string_view>

#include <winrt/base.h>

namespace
{
    struct Probe
    {
        IUIAutomationTreeWalker* walker;
        std::wstring find;
        bool treeItemsOnly = false;
        bool exactName = false;
        bool found = false;

        void Visit(IUIAutomationElement* element, int depth)
        {
            if (depth > 8 || found) return;
            winrt::com_ptr<IUIAutomationElement> child;
            walker->GetFirstChildElement(element, child.put());
            while (child && !found)
            {
                CONTROLTYPEID type = 0;
                BSTR name = nullptr, id = nullptr;
                child->get_CurrentControlType(&type);
                child->get_CurrentName(&name);
                child->get_CurrentAutomationId(&id);
                std::wstring text = name ? name : L"";
                if (find.empty())
                {
                    wprintf(L"%*s%d '%s' id '%s'\n", depth * 2, L"", type, text.c_str(), id ? id : L"");
                }
                else if ((!treeItemsOnly || type == UIA_TreeItemControlTypeId) &&
                    (exactName ? text == find : text.find(find) != std::wstring::npos))
                {
                    RECT r{};
                    child->get_CurrentBoundingRectangle(&r);
                    wprintf(L"%ld %ld\n", (r.left + r.right) / 2, (r.top + r.bottom) / 2);
                    found = true;
                }
                SysFreeString(name);
                SysFreeString(id);
                Visit(child.get(), depth + 1);
                winrt::com_ptr<IUIAutomationElement> next;
                walker->GetNextSiblingElement(child.get(), next.put());
                child = next;
            }
        }
    };
}

namespace
{
    std::wstring Text(BSTR b)
    {
        std::wstring s = b ? std::wstring(b, SysStringLen(b)) : L"";
        SysFreeString(b);
        return s;
    }

    int Focus()
    {
        auto uia = winrt::create_instance<IUIAutomation>(CLSID_CUIAutomation);
        winrt::com_ptr<IUIAutomationElement> focused;
        winrt::check_hresult(uia->GetFocusedElement(focused.put()));
        if (!focused)
        {
            wprintf(L"no focused element\n");
            return 1;
        }
        CONTROLTYPEID type = 0;
        BSTR name = nullptr, cls = nullptr;
        BOOL keyboard = FALSE, focusable = FALSE;
        RECT r{};
        focused->get_CurrentControlType(&type);
        focused->get_CurrentName(&name);
        focused->get_CurrentClassName(&cls);
        focused->get_CurrentHasKeyboardFocus(&keyboard);
        focused->get_CurrentIsKeyboardFocusable(&focusable);
        focused->get_CurrentBoundingRectangle(&r);
        wprintf(L"focused: type %d name '%s' class '%s' keyboard focus %d focusable %d bounds %ld,%ld-%ld,%ld\n", type,
            Text(name).c_str(), Text(cls).c_str(), keyboard, focusable, r.left, r.top, r.right, r.bottom);
        winrt::com_ptr<IUIAutomationTextPattern> pattern;
        focused->GetCurrentPatternAs(UIA_TextPatternId, IID_PPV_ARGS(pattern.put()));
        if (!pattern)
        {
            wprintf(L"no text pattern\n");
            return 1;
        }
        winrt::com_ptr<IUIAutomationTextRange> range;
        winrt::check_hresult(pattern->get_DocumentRange(range.put()));
        BSTR text = nullptr;
        winrt::check_hresult(range->GetText(-1, &text));
        std::wstring content = Text(text);
        for (wchar_t& c : content) if (c == L'\r' || c == L'\n') c = L'|';
        wprintf(L"text pattern: '%s'\n", content.c_str());
        return 0;
    }

    BOOL CALLBACK KeyboardWindow(HWND window, LPARAM)
    {
        DWORD pid = 0;
        GetWindowThreadProcessId(window, &pid);
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        wchar_t path[MAX_PATH]{};
        DWORD size = MAX_PATH;
        if (process)
        {
            QueryFullProcessImageNameW(process, 0, path, &size);
            CloseHandle(process);
        }
        if (!wcsstr(path, L"TextInputHost.exe")) return TRUE;
        wchar_t cls[256]{}, title[256]{};
        GetClassNameW(window, cls, 256);
        GetWindowTextW(window, title, 256);
        DWORD cloaked = 0;
        DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
        RECT r{};
        GetWindowRect(window, &r);
        wprintf(L"keyboard window: class '%s' title '%s' visible %d cloaked %lu bounds %ld,%ld-%ld,%ld\n", cls, title,
            IsWindowVisible(window), cloaked, r.left, r.top, r.right, r.bottom);
        return TRUE;
    }
}

int wmain(int argc, wchar_t** argv)
{
    if (argc < 2) return 2;
    try
    {
        // Physical pixels: a DPI-unaware process would get coordinates scaled to 96 DPI.
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        winrt::init_apartment(winrt::apartment_type::single_threaded);
        if (std::wstring(argv[1]) == L"--focus") return Focus();
        if (std::wstring(argv[1]) == L"--keyboard")
        {
            EnumWindows(KeyboardWindow, 0);
            return 0;
        }
        HWND window = FindWindowW(argv[1], nullptr);
        if (!window)
        {
            fwprintf(stderr, L"no window of class %s\n", argv[1]);
            return 3;
        }
        auto uia = winrt::create_instance<IUIAutomation>(CLSID_CUIAutomation);
        winrt::com_ptr<IUIAutomationElement> root;
        winrt::check_hresult(uia->ElementFromHandle(window, root.put()));
        winrt::com_ptr<IUIAutomationTreeWalker> walker;
        winrt::check_hresult(uia->get_RawViewWalker(walker.put()));
        std::wstring_view option = argc > 3 ? argv[3] : L"";
        Probe probe{ walker.get(), argc > 2 ? argv[2] : L"", option == L"--tree-item", option == L"--exact" };
        probe.Visit(root.get(), 0);
        return probe.find.empty() || probe.found ? 0 : 1;
    }
    catch (winrt::hresult_error const& e)
    {
        fwprintf(stderr, L"UI Automation: 0x%08X %s\n", static_cast<unsigned>(e.code()), e.message().c_str());
        return 4;
    }
}
