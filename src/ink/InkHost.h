#pragma once

// Windows' ink host (IInkDesktopHost): it runs the presenter on a thread of its own, the "ink thread". All presenter
// calls and all ink state go through work items on that thread (QueueWorkItem); work posted from the ink thread itself
// runs at once, in order. Windows ends the ink thread after the host's last release, asynchronously; the thread runs
// code of Windows.UI.Input.Inking.dll, which COM unloads when the apartment that created the host uninitializes - so the
// host waits for the thread's end when it goes (otherwise the thread may run on in an unloaded DLL: access violation).

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
        ~InkHost();
        InkHost(InkHost const&) = delete;
        InkHost& operator=(InkHost const&) = delete;

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
        winrt::handle threadHandle; // the ink thread (SYNCHRONIZE), waited for in the destructor
    };

    /// The presenter asks the app to commit its DirectComposition device (which belongs to the UI thread).
    winrt::com_ptr<IInkCommitRequestHandler> MakeCommitHandler(std::function<void()> commit);
}
