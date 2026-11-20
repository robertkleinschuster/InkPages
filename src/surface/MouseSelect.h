#pragma once

// The mouse on the pages (UI thread; touchpad clicks arrive as the mouse). The mouse never writes: the presenter does
// not take it, its buttons come to the window. A drag beside the selection selects as the selection tool does (lasso
// or rectangle by its mode, with any tool active) - drawn and decided by ToolInput on the ink thread; moving and
// scaling the selection is SelectionController's (it gets the press first). A click selects the app's picture under
// it; a click that neither cleared a selection nor picked a picture is the app's tap (PDF links). With the Text tool
// a click writes or edits a text there (a drag still selects); a click that ended an edit is just that.
// Where the app claims the mouse (its content, e.g. a PDF's text; never on ink) a drag and a double or triple click are
// the app's gesture instead (WritingSurface::mouseGesture); a single click stays a click.

#include <windows.h>

#include <functional>
#include <vector>

#include "ink/InkHost.h"
#include "inkpages/WritingSurface.h"
#include "inkpages/model/ClickCount.h"
#include "inkpages/model/TapGesture.h"

namespace inkpages::detail
{
    class ToolInput;
    class SelectionController;

    /// The app's side of the mouse (Surface wires it; document points).
    struct MouseClaims
    {
        std::function<bool(PointF document)> claims;  // the app claims a drag here (nothing of the library's lies there)
        std::function<bool(PointF document)> inkAt;   // ink lies at the point (a drag there selects it)
        std::function<void(MousePhase phase, PointF press, PointF document, int clicks)> gesture; // to the app
    };

    class MouseSelect
    {
    public:
        MouseSelect(HWND window, InkHost& host, std::function<ToolInput*()> tools, SelectionController& selection,
            std::function<PointF(POINT client)> toDocument, std::function<float()> zoom,
            std::function<void(PointF document)> tapped, std::function<bool(PointF document)> textClick, MouseClaims app)
            : window(window), host(host), tools(std::move(tools)), selection(selection), toDocument(std::move(toDocument)),
              zoom(std::move(zoom)), tapped(std::move(tapped)), textClick(std::move(textClick)), app(std::move(app)) {}

        /// The left button went down beside the selection; `clearedSelection`: the press cleared one (then it is no tap).
        void Press(POINT client, bool clearedSelection);
        void Move(POINT client);
        void Release(POINT client);
        /// WM_CAPTURECHANGED: someone else took the mouse - the gesture ends without selecting.
        void CaptureLost();

    private:
        void End(bool select, PointF at);
        /// The press point is the app's for a drag (it claims it and no ink lies there).
        bool AppTakes(PointF press) const;

        HWND window;
        InkHost& host;
        std::function<ToolInput*()> tools;
        SelectionController& selection;
        std::function<PointF(POINT)> toDocument;
        std::function<float()> zoom;
        std::function<void(PointF)> tapped;
        std::function<bool(PointF)> textClick; // the Text tool takes a click (a text there); false: not the Text tool
        MouseClaims app;
        TapGesture gesture;
        ClickCount clicks;
        std::vector<PointF> path; // until it is a drag
        PointF press, last;       // document
        bool lassoing = false;
        bool consumed = false;
        bool claimable = false;   // the app claimed the press point (asked once a drag begins whether ink lies there)
        bool appGesture = false;  // the gesture is the app's
        int clickCount = 1;
    };
}
