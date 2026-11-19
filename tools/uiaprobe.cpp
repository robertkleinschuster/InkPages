// uiaprobe: what UI Automation (the native client, as Narrator uses it) sees in a window.
//   uiaprobe.exe <window class>            every element, depth first: control type id, name, automation id
//   uiaprobe.exe <window class> <text>     the first element whose name contains <text>: "x y" = its center in screen
//                                          pixels (exit code 1 when there is none) - UI tests click it there
// (The managed System.Windows.Automation client of Windows PowerShell does not bridge comctl32 toolbars: it sees an
// empty pane, while the native client sees the toolbar and its buttons through their MSAA names.)

#include <windows.h>
#include <ole2.h>
#include <uiautomation.h>

#include <cstdio>
#include <string>

#include <winrt/base.h>

namespace
{
    struct Probe
    {
        IUIAutomationTreeWalker* walker;
        std::wstring find;
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
                else if (text.find(find) != std::wstring::npos)
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

int wmain(int argc, wchar_t** argv)
{
    if (argc < 2) return 2;
    try
    {
        // Physical pixels: a DPI-unaware process would get coordinates scaled to 96 DPI.
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        winrt::init_apartment(winrt::apartment_type::single_threaded);
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
        Probe probe{ walker.get(), argc > 2 ? argv[2] : L"" };
        probe.Visit(root.get(), 0);
        return probe.find.empty() || probe.found ? 0 : 1;
    }
    catch (winrt::hresult_error const& e)
    {
        fwprintf(stderr, L"UI Automation: 0x%08X %s\n", static_cast<unsigned>(e.code()), e.message().c_str());
        return 4;
    }
}
