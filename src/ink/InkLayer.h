#pragma once

// The ink presenter: one Windows ink presenter (IInkDesktopHost / IInkPresenterDesktop) over the whole document, its
// visual inside DirectManipulation's content (it follows the transform for drawing and input by itself). It records
// what the presenter collects and erases into the history (strokes cut at the pages' edges first), sets the pens'
// drawing attributes, the processing mode and the pen cursor. Constructor and destructor on the UI thread; everything
// else on the ink thread (InkHost::Post).

#include <dcomp.h>

#include <atomic>
#include <functional>

#include <winrt/Windows.UI.Input.Inking.Core.h>

#include "ink/History.h"
#include "ink/InkHost.h"
#include "ink/InkState.h"
#include "inkpages/model/Pens.h"

namespace inkpages::detail
{
    using winrt::Windows::UI::Input::Inking::InkInputProcessingMode;
    using winrt::Windows::UI::Input::Inking::InkPresenter;
    using winrt::Windows::UI::Input::Inking::Core::CoreInkIndependentInputSource;

    struct PenLook
    {
        Tool tool = Tool::Pen;      // Pen or Highlighter
        Color color{ 0xFF, 0, 0, 0 };
        float width = ToolChoices::DefaultPenWidth;
        friend bool operator==(PenLook const&, PenLook const&) = default;
    };

    class InkLayer
    {
    public:
        /// `ready` and `commit` run on the ink thread (the owner hands them to the UI thread).
        InkLayer(InkHost& host, IDCompositionVisual* root, bool mouseInks, std::function<void()> ready,
            std::function<void()> commit);
        ~InkLayer();
        InkLayer(InkLayer const&) = delete;
        InkLayer& operator=(InkLayer const&) = delete;

        InkPresenter const& Presenter() const { return presenter; }
        CoreInkIndependentInputSource const& Input() const { return input; }
        InkState& State() { return ink; }
        History& Edits() { return history; }
        InkHost& Host() { return host; }

        void SetLayout(PageLayout layout);
        void SetPen(PenLook look);
        void SetMode(InkInputProcessingMode mode);
        /// The pen leaves the presenter (its input goes to the window: the selection is moved there) or comes back.
        void SetPenTaken(bool taken);
        /// The pen cursor (resource id in the app's exe, 0: Windows'), set once the presenter's input is active.
        void SetCursor(UINT resourceId);

        /// Asked for every collected stroke before it is recorded: true drops it (a tap beside the selection).
        std::function<bool(InkStroke const&)> drop;
        /// After strokes were collected and recorded (their pieces on the pages).
        std::function<void(std::vector<Placed> const&)> collected;

    private:
        void Create(IDCompositionVisual* root);
        void Subscribe();
        void ApplyDevices();
        void ApplyCursor();

        InkHost& host;
        bool mouseInks;
        std::function<void()> ready, commit;
        winrt::com_ptr<IInkPresenterDesktop> desktop;
        InkPresenter presenter{ nullptr };
        CoreInkIndependentInputSource input{ nullptr };
        InkState ink;
        History history;
        PenLook pen;
        bool penTaken = false;
        UINT cursor = 0;
        std::atomic<bool> inputActive = false; // the presenter's first pointer event came (Windows' input thread)
    };
}
