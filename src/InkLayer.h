#pragma once

// The ink: one Windows ink presenter (IInkDesktopHost / IInkPresenterDesktop) over the whole document, its visual inside
// the manipulated content (it follows DirectManipulation's transform for drawing and input by itself). Windows runs the
// presenter on the host's own thread ("ink thread"): every presenter call and all ink state live there; other threads
// queue work items (IInkDesktopHost::QueueWorkItem). The presenter's input events (CoreInkIndependentInputSource) come on
// Windows' internal ink thread and are handed over the same way.
//
// Tools: pen and highlighter ink (presenter mode Inking); the whole-stroke eraser is Windows ink's (mode Erasing, and the
// pen's eraser end in mode Inking); the exact eraser runs on unprocessed input (mode None) - with the eraser tool, and
// for the pen's eraser end, taken on its inverted hover before it touches. Every change goes into one undo history.
// Pages: each stroke belongs to the page nearest to its first point; lazy pages hand their strokes to `store` and get
// them back from `load` (page coordinates).

#include <windows.h>
#include <dcomp.h>
#include <inkpresenterdesktop.h>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <vector>

#include <winrt/Windows.UI.Input.Inking.Core.h>

#include "InkState.h"
#include "inkpages/UndoHistory.h"
#include "inkpages/WritingSurface.h"

namespace inkpages::detail
{
    class PartialEraser;

    struct InkMessages
    {
        UINT commit;   // the presenter asks for a DirectComposition commit
        UINT ready;    // the presenter exists
        UINT history;  // the history changed (read History())
        UINT circle;   // the exact eraser's circle: wp = document point (two floats), lp = 1 shown / 0 hidden
    };

    struct HistoryState
    {
        bool canUndo = false, canRedo = false, atSavePoint = true;
        std::set<int> pages; // pages with history: their ink must stay
    };

    class InkLayer
    {
    public:
        InkLayer(HWND window, InkMessages messages, IDCompositionVisual* root, bool mouseInks);
        ~InkLayer();
        InkLayer(InkLayer const&) = delete;
        InkLayer& operator=(InkLayer const&) = delete;

        void SetLayout(PageLayout layout);
        void SetTool(ToolState tool);
        void SetLivePages(std::set<int> const& live);
        void Undo();
        void Redo();
        void MarkSaved();
        HistoryState History() const;

        static PointF UnpackPoint(WPARAM wp);

        std::function<std::vector<InkStroke>(int page)> load;                 // ink thread, page coordinates
        std::function<void(int page, std::vector<InkStroke> strokes)> store;  // ink thread, page coordinates
        std::function<void(int page, InkStroke const& stroke)> collected;     // ink thread, document coordinates

    private:
        void OnInkThread(char const* name, std::function<void()> work);
        void Create(IDCompositionVisual* root, bool mouseInks);
        void Subscribe();
        void ApplyMode();
        void ApplyAttributes();
        void ApplyCursor();
        void Push(std::shared_ptr<Edit> edit);
        void Release(int page);
        void Restore(int page);
        void Circle(PointF at, bool shown);

        HWND window;
        InkMessages messages;
        winrt::com_ptr<IInkDesktopHost> host;
        DWORD hostThread = 0;

        // ink thread only:
        winrt::com_ptr<IInkPresenterDesktop> desktop;
        winrt::Windows::UI::Input::Inking::InkPresenter presenter{ nullptr };
        winrt::Windows::UI::Input::Inking::Core::CoreInkIndependentInputSource input{ nullptr };
        InkState ink;
        UndoHistory history;
        std::unique_ptr<PartialEraser> eraser;
        std::set<int> live;
        ToolState tool;
        bool eraserEndTaken = false;

        // read by Windows' input thread:
        std::atomic<bool> cursorActive = false; // the presenter's input is active (its first pointer event came)
        std::atomic<UINT> cursor = 0;
        std::atomic<bool> partialTool = false;  // the eraser tool erases exactly
        std::atomic<bool> partialEnd = true;    // the pen's eraser end erases exactly

        mutable std::mutex historyLock;
        HistoryState historyState;
    };
}
