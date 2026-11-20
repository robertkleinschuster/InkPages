#include "surface/MouseSelect.h"

#include <format>

#include "inkpages/Log.h"
#include "surface/SelectionController.h"
#include "tools/ToolInput.h"

namespace inkpages::detail
{
    void MouseSelect::Press(POINT client, bool clearedSelection)
    {
        PointF at = toDocument(client);
        // Clicks are counted as Windows counts double clicks (its time and rectangle, at the window's DPI).
        UINT dpi = GetDpiForWindow(window);
        clickCount = clicks.Press({ static_cast<float>(client.x), static_cast<float>(client.y) }, GetMessageTime() & 0xFFFFFFFFull,
            GetDoubleClickTime(), static_cast<float>(GetSystemMetricsForDpi(SM_CXDOUBLECLK, dpi)),
            static_cast<float>(GetSystemMetricsForDpi(SM_CYDOUBLECLK, dpi)));
        gesture = {};
        gesture.Press(0, at, GetTickCount64(), zoom());
        path = { at };
        press = last = at;
        lassoing = false;
        consumed = clearedSelection;
        claimable = app.claims && app.claims(at);
        appGesture = false;
        SetCapture(window); // the drag goes on beside the window
        if (claimable && clickCount >= 2 && AppTakes(at))
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
        path.push_back(at);
        if (!gesture.Moved()) return;
        if (claimable && AppTakes(press))
        {
            appGesture = true; // a drag on the app's content: its gesture, no lasso
            clickCount = 1;
            path.clear();
            Log("mouse: the app's drag begins");
            app.gesture(MousePhase::Begin, press, press, 1);
            app.gesture(MousePhase::Move, press, at, 1);
            return;
        }
        lassoing = true; // a drag, not a click: it selects
        Log("mouse: selection drag begins");
        host.Post("mouse select begin", [tools = tools, points = std::move(path)] {
            if (ToolInput* t = tools()) t->MouseSelectBegin(points);
        });
        path.clear();
    }

    void MouseSelect::Release(POINT client)
    {
        if (!gesture.Active()) return;
        PointF at = toDocument(client);
        auto tap = gesture.Release(0, at, GetTickCount64());
        if (appGesture)
        {
            appGesture = false;
            ReleaseCapture();
            Log("mouse: the app's gesture ends");
            app.gesture(MousePhase::End, press, at, clickCount);
            return;
        }
        bool wasLasso = lassoing;
        End(true, at);
        ReleaseCapture();
        if (wasLasso || !tap) return;
        if (!consumed && textClick && textClick(*tap)) return; // the Text tool: a text there
        if (selection.SelectPictureAt(*tap)) // also when the press cleared a selection: one click, as the pen's tap
        {
            Log("mouse: the click selected a picture");
            return;
        }
        if (consumed)
        {
            Log("mouse: the click cleared the selection");
            return;
        }
        Log(std::format("mouse: tap at {:.1f},{:.1f}", tap->x, tap->y));
        if (tapped) tapped(*tap);
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
        gesture.Cancel();
        gesture = {};
        End(false, {});
    }

    bool MouseSelect::AppTakes(PointF at) const
    {
        if (!app.inkAt || !app.gesture) return false;
        if (app.inkAt(at))
        {
            Log("mouse: ink at the press - the library's selection, not the app's gesture");
            return false;
        }
        return true;
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
