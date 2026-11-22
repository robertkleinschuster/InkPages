#include "surface/MouseSelect.h"

#include <format>
#include <cstdlib>

#include "inkpages/Log.h"
#include "surface/SelectionController.h"
#include "tools/ToolInput.h"

namespace inkpages::detail
{
    void MouseSelect::Press(POINT client, bool clearedSelection, Mode newMode)
    {
        CaptureLost();
        mode = newMode;
        pressClient = client;
        PointF at = toDocument(client);
        // Clicks are counted as Windows counts double clicks (its time and rectangle, at the window's DPI).
        UINT dpi = GetDpiForWindow(window);
        if (mode == Mode::Lasso) clicks = {};
        else clickCount = clicks.Press({ static_cast<float>(client.x), static_cast<float>(client.y) }, GetMessageTime() & 0xFFFFFFFFull,
            GetDoubleClickTime(), static_cast<float>(GetSystemMetricsForDpi(SM_CXDOUBLECLK, dpi)),
            static_cast<float>(GetSystemMetricsForDpi(SM_CYDOUBLECLK, dpi)));
        gesture = {};
        gesture.Press(0, at, GetTickCount64(), zoom());
        path = { at };
        press = last = at;
        lassoing = false;
        consumed = clearedSelection;
        claimable = mode == Mode::Viewer && app.gesture && app.claims && app.claims(at);
        appGesture = false;
        panning = false;
        SetCapture(window); // the drag goes on beside the window
        if (claimable && clickCount >= 2)
        {
            appGesture = true; // a double or triple click on the app's content: its gesture from the press on
            Log(std::format("mouse: the app's gesture begins ({} clicks)", clickCount));
            app.gesture(MousePhase::Begin, press, at, clickCount);
        }
    }

    void MouseSelect::Move(POINT client)
    {
        if (!gesture.Active()) return;
        PointF at = toDocument(client);
        last = at;
        gesture.Move(0, at);
        if (appGesture)
        {
            app.gesture(MousePhase::Move, press, at, clickCount);
            return;
        }
        if (lassoing)
        {
            host.Post("mouse select move", [tools = tools, at] {
                if (ToolInput* t = tools()) t->MouseSelectMove({ at });
            });
            return;
        }
        if (panning) return; // ViewerInput owns the rest of the drag.
        path.push_back(at);
        UINT dpi = GetDpiForWindow(window);
        if (std::abs(client.x - pressClient.x) <= GetSystemMetricsForDpi(SM_CXDRAG, dpi)
            && std::abs(client.y - pressClient.y) <= GetSystemMetricsForDpi(SM_CYDRAG, dpi)) return;
        if (claimable)
        {
            appGesture = true; // a drag on the app's content: its gesture, no lasso
            clickCount = 1;
            path.clear();
            Log("mouse: the app's drag begins");
            app.gesture(MousePhase::Begin, press, press, 1);
            app.gesture(MousePhase::Move, press, at, 1);
            return;
        }
        if (mode == Mode::Viewer)
        {
            panning = true;
            path.clear();
            app.pan(pressClient, client);
            return;
        }
        if (mode == Mode::Lasso) selection.Clear();
        lassoing = true; // a drag, not a click: it selects
        Log("mouse: selection drag begins");
        host.Post("mouse select begin", [tools = tools, points = std::move(path), forceLasso = mode == Mode::Lasso] {
            if (ToolInput* t = tools()) t->MouseSelectBegin(points, forceLasso);
        });
        path.clear();
    }

    bool MouseSelect::Release(POINT client)
    {
        if (!gesture.Active()) return false;
        PointF at = toDocument(client);
        auto tap = gesture.Release(0, at, GetTickCount64());
        if (appGesture)
        {
            appGesture = false;
            ReleaseCapture();
            Log("mouse: the app's gesture ends");
            app.gesture(MousePhase::End, press, at, clickCount);
            return true;
        }
        bool wasDrag = lassoing || panning;
        panning = false;
        End(true, at);
        ReleaseCapture();
        if (mode == Mode::Lasso) return wasDrag; // an ordinary right click opens the native context menu
        if (mode == Mode::CopyArea) return true;
        if (wasDrag || !tap) return true;
        if (!consumed && textClick && textClick(*tap)) return true; // the Text tool: a text there
        if (selection.SelectPictureAt(*tap)) // also when the press cleared a selection: one click, as the pen's tap
        {
            Log("mouse: the click selected a picture");
            return true;
        }
        if (consumed)
        {
            Log("mouse: the click cleared the selection");
            return true;
        }
        Log(std::format("mouse: tap at {:.1f},{:.1f}", tap->x, tap->y));
        if (tapped) tapped(*tap);
        return true;
    }

    void MouseSelect::CaptureLost()
    {
        if (appGesture)
        {
            Log("mouse: capture lost, the app's gesture is cancelled");
            appGesture = false;
            gesture = {};
            app.gesture(MousePhase::Cancel, press, last, clickCount);
            return;
        }
        if (!gesture.Active() && !lassoing) return;
        Log("mouse: capture lost, the gesture ends");
        panning = false;
        gesture.Cancel();
        gesture = {};
        End(false, {});
    }

    void MouseSelect::End(bool select, PointF at)
    {
        path.clear();
        if (!lassoing) return;
        lassoing = false;
        host.Post("mouse select end", [tools = tools, select, at] {
            if (ToolInput* t = tools()) select ? t->MouseSelectEnd(at) : t->MouseSelectCancel();
        });
    }
}
