#pragma once

// Work handed to the UI thread from other threads (the ink host thread, Windows' input thread): a queue drained when the
// owner's window receives `message` (posted once per batch). Dropped work never runs after the owner is gone: the
// queue lives as long as the window's owner.

#include <windows.h>

#include <deque>
#include <functional>
#include <mutex>

namespace inkpages::detail
{
    class UiThread
    {
    public:
        UiThread(HWND window, UINT message) : window(window), message(message) {}

        void Post(std::function<void()> work);
        /// The owner's window procedure calls this on `message`.
        void Drain();

    private:
        HWND window;
        UINT message;
        std::mutex lock;
        std::deque<std::function<void()>> queue;
    };
}
