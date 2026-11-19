// The writing surface's window procedure: input to the module it belongs to - touch and touchpad to DirectManipulation
// (finger taps watched by TouchTaps), the pen given to the window to the selection, the mouse to the selection and
// then to MouseSelect, the wheel to DirectManipulation - and the UI thread's handed-over work.

#include "surface/Surface.h"

#include <windowsx.h>

#include <format>

#include "inkpages/Log.h"

namespace inkpages::detail
{
    namespace
    {
        constexpr UINT UiMessage = WM_APP + 0x101;
        constexpr UINT_PTR MotionTimer = 2, ScrollTimer = 3;

        POINTER_INPUT_TYPE TypeOf(WPARAM wp)
        {
            POINTER_INPUT_TYPE type = PT_POINTER;
            if (!GetPointerType(GET_POINTERID_WPARAM(wp), &type))
            {
                LogHr("GetPointerType", HRESULT_FROM_WIN32(GetLastError()));
            }
            return type;
        }

        bool IsPen(WPARAM wp) { return TypeOf(wp) == PT_PEN; }

        // [doc] Mouse messages Windows makes from pen or touch input carry this signature in GetMessageExtraInfo
        // ("Distinguishing Pen Input from Mouse and Touch"): only the real mouse (and touchpad clicks) is the mouse here.
        bool IsRealMouse()
        {
            constexpr LPARAM Mask = static_cast<LPARAM>(0xFFFFFF00), Signature = 0xFF515700;
            return (GetMessageExtraInfo() & Mask) != Signature;
        }

        POINT ClientOf(HWND window, LPARAM lp)
        {
            POINT p{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            ScreenToClient(window, &p); // pointer messages carry screen coordinates
            return p;
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
            if (viewport && viewport->OnPointer(wp)) // touch and touchpad pan and zoom
            {
                if (TypeOf(wp) == PT_TOUCH) touchTaps->Down(GET_POINTERID_WPARAM(wp), ClientOf(window, lp));
                return 0;
            }
            if (IsPen(wp) && selection->OnPointer(message, wp, lp)) return 0;
            break;
        case DM_POINTERHITTEST:
            if (viewport && viewport->OnPointer(wp)) return 0;
            break;
        case WM_POINTERUPDATE:
        case WM_POINTERUP:
        {
            POINTER_INPUT_TYPE type = TypeOf(wp);
            if (type == PT_TOUCH)
            {
                if (message == WM_POINTERUP) touchTaps->Up(GET_POINTERID_WPARAM(wp), ClientOf(window, lp));
                else touchTaps->Update(GET_POINTERID_WPARAM(wp), ClientOf(window, lp));
            }
            if (type == PT_PEN && selection->OnPointer(message, wp, lp)) return 0;
            break;
        }
        case WM_POINTERCAPTURECHANGED:
            touchTaps->Cancel("DirectManipulation took the contact");
            break;
        case WM_LBUTTONDOWN:
        {
            if (!IsRealMouse()) break;
            bool had = selection->HasSelection();
            if (selection->OnPointer(message, wp, lp)) return 0; // on the selection: move or scale
            mouse->Press({ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) }, had);
            return 0;
        }
        case WM_MOUSEMOVE:
            if (!IsRealMouse()) break;
            if (selection->OnPointer(message, wp, lp)) return 0;
            if (wp & MK_LBUTTON) mouse->Move({ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) });
            return 0;
        case WM_LBUTTONUP:
            if (!IsRealMouse()) break;
            if (selection->OnPointer(message, wp, lp)) return 0;
            mouse->Release({ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) });
            return 0;
        case WM_CAPTURECHANGED:
            if (reinterpret_cast<HWND>(lp) != window) mouse->CaptureLost();
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
