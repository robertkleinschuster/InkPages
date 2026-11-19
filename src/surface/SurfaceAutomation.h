#pragma once

// UI Automation for the surface's window (UI thread): a fragment root hosted on the window (WM_GETOBJECT,
// UiaRootObjectId) whose only child, while a text is edited, is the windowless RichEdit's own provider - an edit
// control with the text pattern, focused. That is what Windows' touch keyboard (auto-invoke on a tap into a text
// field), Narrator and Magnifier look for. Microsoft's route for windowless RichEdit: the host passes a windowless site
// (IRawElementProviderWindowlessSite: the parent fragment and a runtime id prefix; IRichEditUiaInformation: the box on
// the screen) to IRicheditWindowlessAccessibility::CreateProvider.
// https://learn.microsoft.com/windows/win32/api/textserv/nn-textserv-iricheditwindowlessaccessibility

#include <windows.h>
#include <ole2.h> // before uiautomation.h: WIN32_LEAN_AND_MEAN leaves out what it needs
#include <uiautomation.h>

#include <functional>
#include <memory>

#include <winrt/base.h>

namespace inkpages::detail
{
    class RichEditBox;

    class SurfaceAutomation
    {
    public:
        explicit SurfaceAutomation(HWND window);
        ~SurfaceAutomation();
        SurfaceAutomation(SurfaceAutomation const&) = delete;
        SurfaceAutomation& operator=(SurfaceAutomation const&) = delete;

        /// WM_GETOBJECT: true when it answered (`result` for the window procedure).
        bool OnGetObject(WPARAM wp, LPARAM lp, LRESULT& result);

        /// A text box is edited: its RichEdit becomes the window's focused child. `screen`: the box in screen pixels.
        void TextBoxBegins(RichEditBox& box, std::function<RECT()> screen);
        void TextBoxEnds();

        struct State; // shared with the COM objects (UI Automation may hold them longer than the surface)

    private:
        HWND window;
        std::shared_ptr<State> state;
        winrt::com_ptr<IRawElementProviderSimple> root;
    };
}
