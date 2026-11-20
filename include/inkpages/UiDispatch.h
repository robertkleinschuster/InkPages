#pragma once

#include <winrt/Windows.System.h>

#include <atomic>
#include <functional>
#include <memory>

namespace inkpages
{
    // Ensures a Windows dispatcher queue is present on the current UI thread. Keep the scope around the message loop.
    // A pre-existing queue is left under its original owner's control.
    class UiDispatcherScope
    {
    public:
        UiDispatcherScope();
        ~UiDispatcherScope();
        UiDispatcherScope(UiDispatcherScope const&) = delete;
        UiDispatcherScope& operator=(UiDispatcherScope const&) = delete;

    private:
        struct QueueOwner;
        std::shared_ptr<QueueOwner> owner;
    };

    // A window's work submitted to the thread dispatcher. Close on the UI thread before destroying the window's state.
    class UiDispatch
    {
    public:
        UiDispatch();
        ~UiDispatch();
        UiDispatch(UiDispatch const&) = delete;
        UiDispatch& operator=(UiDispatch const&) = delete;

        bool Post(std::function<void()> work) const;
        void Close() noexcept;

    private:
        winrt::Windows::System::DispatcherQueue queue;
        std::shared_ptr<std::atomic<bool>> open = std::make_shared<std::atomic<bool>>(true);
    };
}
