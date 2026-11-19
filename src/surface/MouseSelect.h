#pragma once

// The mouse on the pages (UI thread; touchpad clicks arrive as the mouse). The mouse never writes: the presenter does
// not take it, its buttons come to the window. A drag beside the selection selects as the selection tool does (lasso
// or rectangle by its mode, with any tool active) - drawn and decided by ToolInput on the ink thread; moving and
// scaling the selection is SelectionController's (it gets the press first). A click selects the app's picture under
// it; a click that neither cleared a selection nor picked a picture is the app's tap (PDF links).

#include <windows.h>

#include <functional>
#include <vector>

#include "ink/InkHost.h"
#include "inkpages/model/TapGesture.h"

namespace inkpages::detail
{
    class ToolInput;
    class SelectionController;

    class MouseSelect
    {
    public:
        MouseSelect(HWND window, InkHost& host, std::function<ToolInput*()> tools, SelectionController& selection,
            std::function<PointF(POINT client)> toDocument, std::function<float()> zoom,
            std::function<void(PointF document)> tapped)
            : window(window), host(host), tools(std::move(tools)), selection(selection), toDocument(std::move(toDocument)),
              zoom(std::move(zoom)), tapped(std::move(tapped)) {}

        /// The left button went down beside the selection; `clearedSelection`: the press cleared one (then it is no tap).
        void Press(POINT client, bool clearedSelection);
        void Move(POINT client);
        void Release(POINT client);
        /// WM_CAPTURECHANGED: someone else took the mouse - the gesture ends without selecting.
        void CaptureLost();

    private:
        void End(bool select, PointF at);

        HWND window;
        InkHost& host;
        std::function<ToolInput*()> tools;
        SelectionController& selection;
        std::function<PointF(POINT)> toDocument;
        std::function<float()> zoom;
        std::function<void(PointF)> tapped;
        TapGesture gesture;
        std::vector<PointF> path; // until it is a drag
        bool lassoing = false;
        bool consumed = false;
    };
}
