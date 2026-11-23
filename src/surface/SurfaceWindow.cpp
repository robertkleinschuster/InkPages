// The writing surface's window procedure: input to the module it belongs to - touch and touchpad to DirectManipulation
// (finger taps watched by TouchTaps), the pen given to the window to the selection, the mouse to the selection and
// then to MouseSelect, a text being edited first to the TextEditor, the wheel to DirectManipulation, the viewer's keys,
// middle-button pan and hover cursor to ViewerInput - and the UI thread's handed-over work.

#include "surface/Surface.h"
#include "WindowData.h"

#include <windowsx.h>

#include <format>

#include "inkpages/Log.h"

namespace inkpages::detail
{
    void Surface::SetWorkspaceColor(Color color)
    {
        if (options.background == color) return;
        options.background = color;
        winrt::check_bool(RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE));
    }

    namespace
    {
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

        // [doc] Mouse messages Windows makes from pen or touch input carry this signature in GetMessageExtraInfo
        // ("Distinguishing Pen Input from Mouse and Touch"): only the real mouse (and touchpad clicks) is the mouse here.
        bool IsRealMouse()
        {
            constexpr LPARAM Mask = static_cast<LPARAM>(0xFFFFFF00), Signature = 0xFF515700;
            return (GetMessageExtraInfo() & Mask) != Signature;
        }

        // A mouse press puts the keyboard on the pages (scroll keys), as a viewer's page area takes the focus when clicked.
        void TakeFocus(HWND window)
        {
            if (GetFocus() != window) SetFocus(window);
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
            if (!SetWindowData(window, reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams)) return FALSE;
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
        // A text being edited takes the keyboard and the mouse (and the pen's mouse messages) in its box first.
        bool wasEditing = textEditor && textEditor->Editing();
        if (wasEditing)
        {
            LRESULT result = 0;
            if (textEditor->OnMessage(message, wp, lp, result)) return result;
        }
        if (!options.allowViewportInput)
        {
            switch (message)
            {
            case WM_POINTERDOWN:
            case WM_POINTERUPDATE:
            case WM_POINTERUP:
                if (TypeOf(wp) == PT_PEN) break;
                return DefWindowProcW(window, message, wp, lp);
            case DM_POINTERHITTEST:
            case WM_MOUSEWHEEL:
            case WM_MOUSEHWHEEL:
            case WM_KEYDOWN:
            case WM_LBUTTONDOWN:
            case WM_LBUTTONDBLCLK:
            case WM_MBUTTONDOWN:
            case WM_RBUTTONDOWN:
                return DefWindowProcW(window, message, wp, lp);
            }
        }
        switch (message)
        {
        case WM_POINTERDOWN:
        {
            UINT32 id = GET_POINTERID_WPARAM(wp);
            POINTER_INPUT_TYPE type = TypeOf(wp);
            if (type == PT_TOUCH)
            {
                if (touchSelection)
                {
                    // The second finger takes over as a pinch. Undo the preview and give both contacts to Windows.
                    UINT32 first = touchSelection;
                    touchSelection = 0;
                    selection->EndTouchDrag(true);
                    viewport->OnPointer(first);
                    touchTaps->Cancel("the second contact takes the selection drag");
                }
                else if (IS_POINTER_PRIMARY_WPARAM(wp) && currentTool == Tool::Select && !viewport->Moving())
                {
                    POINT client = ClientOf(window, lp);
                    if (selection->Grabs(ToDocument(client)))
                    {
                        touchSelection = id;
                        selection->BeginTouchDrag(client);
                        return 0;
                    }
                }
            }
            if (viewport && viewport->OnPointer(wp)) // touch and touchpad pan and zoom
            {
                // A finger that stops the pages' inertia (or joins a running pan) is no tap.
                if (type == PT_TOUCH && !viewport->Moving() && (IS_POINTER_PRIMARY_WPARAM(wp) || touchTaps->Active()))
                    touchTaps->Down(id, ClientOf(window, lp));
                return 0;
            }
            if (type == PT_PEN && !touchSelection && selection->OnPointer(message, wp, lp)) return 0;
            break;
        }
        case DM_POINTERHITTEST:
            if (touchSelection == GET_POINTERID_WPARAM(wp)) return 0;
            if (currentTool == Tool::Select && selection->HasSelection() && !viewport->Moving())
            {
                POINTER_INFO info{};
                if (!GetPointerInfo(GET_POINTERID_WPARAM(wp), &info))
                    LogHr("GetPointerInfo (selection hit test)", HRESULT_FROM_WIN32(GetLastError()));
                else if (info.pointerType == PT_TOUCH && (info.pointerFlags & POINTER_FLAG_PRIMARY))
                {
                    POINT client = info.ptPixelLocation;
                    if (!ScreenToClient(window, &client))
                        LogHr("ScreenToClient (selection hit test)", HRESULT_FROM_WIN32(GetLastError()));
                    else if (selection->Grabs(ToDocument(client))) return 0;
                }
            }
            if (viewport && viewport->OnPointer(wp)) return 0;
            break;
        case WM_POINTERUPDATE:
        case WM_POINTERUP:
        {
            POINTER_INPUT_TYPE type = TypeOf(wp);
            if (type == PT_TOUCH)
            {
                bool cancelled = IS_POINTER_CANCELED_WPARAM(wp);
                if (touchSelection == GET_POINTERID_WPARAM(wp))
                {
                    if (message == WM_POINTERUP || cancelled)
                    {
                        touchSelection = 0;
                        selection->EndTouchDrag(cancelled);
                    }
                    else selection->UpdateTouchDrag(ClientOf(window, lp));
                    return 0;
                }
                if (cancelled)
                {
                    touchTaps->Cancel("Windows canceled the contact");
                    return 0;
                }
                if (message == WM_POINTERUP) touchTaps->Up(GET_POINTERID_WPARAM(wp), ClientOf(window, lp));
                else touchTaps->Update(GET_POINTERID_WPARAM(wp), ClientOf(window, lp));
            }
            if (type == PT_PEN && !touchSelection && selection->OnPointer(message, wp, lp)) return 0;
            break;
        }
        case WM_POINTERCAPTURECHANGED:
            if (touchSelection == GET_POINTERID_WPARAM(wp))
            {
                touchSelection = 0;
                selection->EndTouchDrag(true);
            }
            if (TypeOf(wp) == PT_PEN) selection->CancelDrag();
            touchTaps->Cancel("DirectManipulation took the contact");
            break;
        case WM_LBUTTONDOWN:
        case WM_LBUTTONDBLCLK: // (the class has CS_DBLCLKS for the text box: outside it a press is a press)
        {
            if (!IsRealMouse()) break;
            if (touchSelection || viewer->Panning() || mouse->UsesRightButton()) return 0;
            TakeFocus(window);
            // A press that cleared the selection or ended a text's edit is no click of its own.
            bool had = selection->HasSelection() || (wasEditing && !textEditor->Editing());
            if (currentTool != Tool::CopyArea && selection->OnPointer(message, wp, lp)) return 0;
            mouse->Press({ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) }, had,
                currentTool == Tool::CopyArea ? MouseSelect::Mode::CopyArea :
                currentTool == Tool::Select ? MouseSelect::Mode::Select : MouseSelect::Mode::Viewer);
            return 0;
        }
        case WM_MOUSEMOVE:
        {
            if (!IsRealMouse())
            {
                viewer->NotHovering();
                break;
            }
            POINT at{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            if (touchSelection) return 0;
            if (viewer->PanMove(at)) return 0;
            if (currentTool != Tool::CopyArea && selection->OnPointer(message, wp, lp)) return 0;
            if (wp & (mouse->UsesRightButton() ? MK_RBUTTON : MK_LBUTTON)) mouse->Move(at);
            if (wp & (MK_LBUTTON | MK_RBUTTON | MK_MBUTTON | MK_XBUTTON1 | MK_XBUTTON2)) viewer->NotHovering();
            else viewer->Hover(at);
            return 0;
        }
        case WM_MOUSELEAVE:
            viewer->Left();
            return 0;
        case WM_SETCURSOR:
            if (LOWORD(lp) == HTCLIENT && viewer->OnSetCursor()) return TRUE;
            break;
        case WM_MBUTTONDOWN:
            if (!IsRealMouse()) break;
            if (touchSelection || (wp & (MK_LBUTTON | MK_RBUTTON))) return 0;
            TakeFocus(window);
            viewer->PanDown({ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) });
            return 0;
        case WM_MBUTTONUP:
            if (viewer->PanUp()) return 0;
            break;
        case WM_RBUTTONDOWN:
            if (!IsRealMouse()) break;
            if (touchSelection || viewer->Panning() || (wp & MK_LBUTTON)) return 0;
            TakeFocus(window);
            mouse->Press({ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) }, false, MouseSelect::Mode::Lasso);
            return 0;
        case WM_RBUTTONUP:
            // The interaction context takes touch secondary taps, including touch promoted to mouse input.
            if ((GetMessageExtraInfo() & 0xFFFFFF80) == 0xFF515780) return 0;
            if (IsRealMouse() && mouse->UsesRightButton()
                && mouse->Release({ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) })) return 0;
            break;
        case WM_KEYDOWN:
            if (selection->OnKey(wp)) return 0;
            if (viewer->OnKey(wp, lp)) return 0;
            break;
        case WM_LBUTTONUP:
            if (!IsRealMouse()) break;
            if (touchSelection || mouse->UsesRightButton()) return 0;
            if (currentTool != Tool::CopyArea && selection->OnPointer(message, wp, lp)) return 0;
            mouse->Release({ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) });
            return 0;
        case WM_CAPTURECHANGED:
            if (reinterpret_cast<HWND>(lp) != window)
            {
                selection->CancelDrag();
                mouse->CaptureLost();
                viewer->CaptureLost();
            }
            break;
        case WM_MOUSEWHEEL:
        case WM_MOUSEHWHEEL:
        {
            MSG msg{ window, message, wp, lp };
            if (viewport && viewport->OnWheel(msg)) return 0;
            break;
        }
        case WM_GETOBJECT:
        {
            LRESULT result = 0;
            if (automation && automation->OnGetObject(wp, lp, result)) return result;
            break;
        }
        case WM_SIZE:
            Resized();
            return 0;
        case WM_DPICHANGED_AFTERPARENT:
            SetScale();
            Resized();
            return 0;
        case WM_TIMER:
            if (wp == MotionTimer) Changed(false);
            else if (wp == ScrollTimer) selection->OnScrollTimer();
            else if (wp == LaserTimer && laserTimerActive) DrawOverlay();
            return 0;
        case WM_ERASEBKGND:
        {
            RECT rc{};
            winrt::check_bool(GetClientRect(window, &rc));
            Color c = options.background;
            auto dc = reinterpret_cast<HDC>(wp);
            SetDCBrushColor(dc, RGB(c.r, c.g, c.b));
            winrt::check_bool(FillRect(dc, &rc, static_cast<HBRUSH>(GetStockObject(DC_BRUSH))));
            return 1;
        }
        }
        return DefWindowProcW(window, message, wp, lp);
    }
}
