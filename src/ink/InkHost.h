#pragma once

// Windows' ink host (IInkDesktopHost): it runs the presenter on a thread of its own, the "ink thread". All presenter
// calls and all ink state go through work items on that thread (QueueWorkItem); work posted from the ink thread itself
// runs at once, in order.

#include <windows.h>
#include <inkpresenterdesktop.h>

#include <atomic>
#include <functional>

#include <winrt/base.h>

namespace inkpages::detail
{
    class InkHost
    {
    public:
        InkHost();

        IInkDesktopHost* Get() const { return host.get(); }
        void Post(char const* name, std::function<void()> work);
        /// Runs `work` on the ink thread and waits for it (snapshots; INFINITE at shutdown: the work refers to objects
        /// that are about to go). The work's exception comes to the caller; a timeout throws hresult_error
        /// (HRESULT_FROM_WIN32(ERROR_TIMEOUT)) - the work may still run later, so it must hold what it touches.
        void Run(char const* name, std::function<void()> work, DWORD timeoutMs = 3000);
        bool OnThread() const { return GetCurrentThreadId() == thread.load(); }

    private:
        winrt::com_ptr<IInkDesktopHost> host;
        std::atomic<DWORD> thread = 0;
    };

    /// The presenter asks the app to commit its DirectComposition device (which belongs to the UI thread).
    winrt::com_ptr<IInkCommitRequestHandler> MakeCommitHandler(std::function<void()> commit);
}
