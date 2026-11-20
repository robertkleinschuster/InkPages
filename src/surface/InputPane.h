#pragma once

#include <shobjidl.h>
#include <winrt/base.h>

#include <functional>
#include <optional>

namespace inkpages::detail
{
    // The desktop input pane's occluded screen rectangle. Register on the owning UI thread.
    class InputPane
    {
    public:
        InputPane(HWND window, std::function<void(std::optional<RECT>)> changed);
        ~InputPane();
        InputPane(InputPane const&) = delete;
        InputPane& operator=(InputPane const&) = delete;

        std::optional<RECT> Location() const;

    private:
        struct Handler;
        winrt::com_ptr<IFrameworkInputPane> pane;
        winrt::com_ptr<Handler> handler;
        DWORD cookie = 0;
    };
}
