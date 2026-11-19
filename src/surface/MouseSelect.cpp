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
        gesture = {};
        gesture.Press(0, at, GetTickCount64());
        path = { at };
        lassoing = false;
        consumed = clearedSelection;
        SetCapture(window); // the drag goes on beside the window
    }

    void MouseSelect::Move(POINT client)
    {
        if (!gesture.Active()) return;
        PointF at = toDocument(client);
        gesture.Move(0, at);
        if (lassoing)
        {
            host.Post("mouse select move", [tools = tools, at] {
                if (ToolInput* t = tools()) t->MouseSelectMove({ at });
            });
            return;
        }
        path.push_back(at);
        if (!gesture.Moved()) return;
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
        bool wasLasso = lassoing;
        End(true, at);
        ReleaseCapture();
        if (wasLasso || !tap) return;
        if (consumed)
        {
            Log("mouse: the click cleared the selection");
            return;
        }
        if (selection.SelectPictureAt(*tap))
        {
            Log("mouse: the click selected a picture");
            return;
        }
        Log(std::format("mouse: tap at {:.1f},{:.1f}", tap->x, tap->y));
        if (tapped) tapped(*tap);
    }

    void MouseSelect::CaptureLost()
    {
        if (!gesture.Active() && !lassoing) return;
        Log("mouse: capture lost, the gesture ends");
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
