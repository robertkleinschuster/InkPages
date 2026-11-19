#pragma once

// The ink: one Windows ink presenter (IInkDesktopHost / IInkPresenterDesktop) over the whole document, its visual inside
// the manipulated content (it follows DirectManipulation's transform for drawing and input by itself). Windows runs the
// presenter on the host's own thread: every presenter call goes through a work item (IInkDesktopHost::QueueWorkItem);
// the callbacks below run on that thread.
//
// Pages: each stroke belongs to the page nearest to its first point. Lazy pages: a page released from the ink hands its
// strokes (in page coordinates) to `store` and they leave the presenter; a page that comes near gets them back from
// `load`.

#include <windows.h>
#include <dcomp.h>
#include <inkpresenterdesktop.h>

#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <vector>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.Input.Inking.h>
#include <winrt/Windows.UI.Input.Inking.Core.h>

#include "inkpages/PageLayout.h"

namespace inkpages::detail
{
    using winrt::Windows::UI::Input::Inking::InkStroke;

    class InkLayer
    {
    public:
        /// `window` receives `commitMessage` whenever the presenter asks for a DirectComposition commit, and
        /// `readyMessage` once the presenter exists.
        InkLayer(HWND window, UINT commitMessage, UINT readyMessage, IDCompositionVisual* root, SizeF document, bool mouseInks);
        ~InkLayer();
        InkLayer(InkLayer const&) = delete;
        InkLayer& operator=(InkLayer const&) = delete;

        void SetLayout(PageLayout layout);
        void SetPen(winrt::Windows::UI::Color color, float size);
        /// The pen cursor: a cursor resource id of the app's exe (0: Windows' default). Applied once the presenter's
        /// input is active (`ActivateCursor`).
        void SetCursor(UINT resourceId);
        /// Measured: setting PointerCursor while Windows still activates the presenter's input context fails fast
        /// (0xC0000409 in Windows.UI.Input.Inking, CInputContext::_RunOnInkThreadSync E_UNEXPECTED). Called a moment
        /// after the presenter is ready.
        void ActivateCursor();

        /// Gives pages their strokes back (load) or takes them away (store) - the pages that keep their ink.
        void SetLivePages(std::set<int> const& live);

        std::function<std::vector<InkStroke>(int page)> load;                 // ink thread, page coordinates
        std::function<void(int page, std::vector<InkStroke> strokes)> store;  // ink thread, page coordinates
        std::function<void(int page, InkStroke const& stroke)> collected;     // ink thread, document coordinates

    private:
        void OnInkThread(char const* name, std::function<void()> work);
        void Create(IDCompositionVisual* root, SizeF document, bool mouseInks);
        void ApplyCursor();
        void Release(int page);
        void Restore(int page);

        HWND window;
        UINT commitMessage, readyMessage;
        winrt::com_ptr<IInkDesktopHost> host;
        // ink thread only:
        winrt::com_ptr<IInkPresenterDesktop> desktop;
        winrt::Windows::UI::Input::Inking::InkPresenter presenter{ nullptr };
        winrt::Windows::UI::Input::Inking::Core::CoreInkIndependentInputSource input{ nullptr };
        PageLayout layout;
        std::map<uint32_t, int> owner; // stroke id -> page
        std::set<int> live;
        UINT cursor = 0;
        bool cursorActive = false;
    };

    /// A copy of the stroke moved by (dx, dy): new ink points, the same drawing attributes, no point transform.
    InkStroke Translated(InkStroke const& stroke, float dx, float dy);
}
