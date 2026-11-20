#pragma once

#include "inkpages/UiDispatch.h"

namespace inkpages::detail
{
    class UiThread
    {
    public:
        bool Post(std::function<void()> work);
        void Close() noexcept { dispatch->Close(); }
        std::weak_ptr<UiDispatch> Dispatcher() const { return dispatch; }

    private:
        UiDispatcherScope scope;
        std::shared_ptr<UiDispatch> dispatch = std::make_shared<UiDispatch>();
    };
}
