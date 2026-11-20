#include "surface/InputPane.h"

#include "inkpages/Log.h"

namespace inkpages::detail
{
    struct InputPane::Handler : winrt::implements<Handler, IFrameworkInputPaneHandler>
    {
        explicit Handler(std::function<void(std::optional<RECT>)> changed) : changed(std::move(changed)) {}

        HRESULT STDMETHODCALLTYPE Showing(RECT* screen, BOOL) noexcept override
        {
            Log("input pane: showing");
            return Notify(screen ? std::optional<RECT>(*screen) : std::nullopt);
        }

        HRESULT STDMETHODCALLTYPE Hiding(BOOL) noexcept override
        {
            Log("input pane: hiding");
            return Notify(std::nullopt);
        }

        HRESULT Notify(std::optional<RECT> screen) noexcept
        {
            try
            {
                if (changed) changed(screen);
                return S_OK;
            }
            catch (...)
            {
                LogException("input pane: notifying the writing surface");
                return winrt::to_hresult();
            }
        }

        std::function<void(std::optional<RECT>)> changed;
    };

    InputPane::InputPane(HWND window, std::function<void(std::optional<RECT>)> changed)
        : pane(winrt::create_instance<IFrameworkInputPane>(__uuidof(FrameworkInputPane))),
          handler(winrt::make_self<Handler>(std::move(changed)))
    {
        winrt::check_hresult(LogHr("input pane: AdviseWithHWND",
            pane->AdviseWithHWND(window, handler.as<IFrameworkInputPaneHandler>().get(), &cookie)));
    }

    InputPane::~InputPane()
    {
        handler->changed = nullptr;
        LogHr("input pane: Unadvise", pane->Unadvise(cookie));
    }

    std::optional<RECT> InputPane::Location() const
    {
        RECT screen{};
        winrt::check_hresult(pane->Location(&screen));
        if (IsRectEmpty(&screen)) return std::nullopt;
        return screen;
    }
}
