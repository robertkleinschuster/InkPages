#include "ink/UiThread.h"

#include "inkpages/Log.h"

namespace inkpages::detail
{
    void UiThread::Post(std::function<void()> work)
    {
        bool first;
        {
            std::scoped_lock guard(lock);
            first = queue.empty();
            queue.push_back(std::move(work));
        }
        if (first && !PostMessageW(window, message, 0, 0)) LogHr("UiThread PostMessage", HRESULT_FROM_WIN32(GetLastError()));
    }

    void UiThread::Drain()
    {
        std::deque<std::function<void()>> work;
        {
            std::scoped_lock guard(lock);
            work.swap(queue);
        }
        for (auto& item : work)
        {
            try
            {
                item();
            }
            catch (...)
            {
                LogException("UI thread work");
            }
        }
    }
}
