// The writing surface's window procedure: input to the module it belongs to - touch and touchpad to DirectManipulation,
// the pen given to the window and the mouse to the selection, the wheel to DirectManipulation - and the UI thread's
// handed-over work.

#include "surface/Surface.h"

#include <format>

#include "inkpages/Log.h"

namespace inkpages::detail
{
    namespace
    {
        constexpr UINT UiMessage = WM_APP + 0x101;
        constexpr UINT_PTR MotionTimer = 2, ScrollTimer = 3;

        bool IsPen(WPARAM wp)
        {
            POINTER_INPUT_TYPE type = PT_POINTER;
            if (!GetPointerType(GET_POINTERID_WPARAM(wp), &type))
            {
                LogHr("GetPointerType", HRESULT_FROM_WIN32(GetLastError()));
                return false;
            }
            return type == PT_PEN;
        }
    }

    LRESULT CALLBACK Surface::WindowProc(HWND window, UINT message, WPARAM wp, LPARAM lp)
    {
        if (message == WM_NCCREATE)
        {
            SetWindowLongPtrW(window, GWLP_USERDATA,
                reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams));
        }
        auto self = reinterpret_cast<Surface*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (self && self->composition)
        {
            try
            {
                return self->Handle(message, wp, lp);
            }
            catch (...)
            {
                LogException(std::format("surface: message 0x{:X}", message));
                return 0;
            }
        }
        return DefWindowProcW(window, message, wp, lp);
    }

    LRESULT Surface::Handle(UINT message, WPARAM wp, LPARAM lp)
    {
        switch (message)
        {
        case WM_POINTERDOWN:
            if (viewport && viewport->OnPointer(wp)) return 0; // touch and touchpad pan and zoom
            if (IsPen(wp) && selection->OnPointer(message, wp, lp)) return 0;
            break;
        case DM_POINTERHITTEST:
            if (viewport && viewport->OnPointer(wp)) return 0;
            break;
        case WM_POINTERUPDATE:
        case WM_POINTERUP:
            if (IsPen(wp) && selection->OnPointer(message, wp, lp)) return 0;
            break;
        case WM_LBUTTONDOWN:
        case WM_MOUSEMOVE:
        case WM_LBUTTONUP:
            if (selection->OnPointer(message, wp, lp)) return 0;
            break;
        case WM_MOUSEWHEEL:
        case WM_MOUSEHWHEEL:
        {
            MSG msg{ window, message, wp, lp };
            if (viewport && viewport->OnWheel(msg)) return 0;
            break;
        }
        case WM_SIZE:
            Resized();
            return 0;
        case WM_DPICHANGED_AFTERPARENT:
            SetScale();
            Resized();
            return 0;
        case UiMessage:
            ui->Drain();
            return 0;
        case WM_TIMER:
            if (wp == MotionTimer) Changed(false);
            else if (wp == ScrollTimer) selection->OnScrollTimer();
            return 0;
        case WM_ERASEBKGND:
        {
            RECT rc;
            GetClientRect(window, &rc);
            Color c = options.background;
            HBRUSH brush = CreateSolidBrush(RGB(c.r, c.g, c.b));
            FillRect(reinterpret_cast<HDC>(wp), &rc, brush);
            DeleteObject(brush);
            return 1;
        }
        }
        return DefWindowProcW(window, message, wp, lp);
    }
}
