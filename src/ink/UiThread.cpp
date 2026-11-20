#include "ink/UiThread.h"

#include "inkpages/Log.h"

#include <dispatcherqueue.h>
#include <winrt/Windows.Foundation.h>

#include <optional>

using winrt::Windows::System::DispatcherQueue;
using winrt::Windows::System::DispatcherQueueController;

namespace inkpages
{
    struct UiDispatcherScope::QueueOwner
    {
        DispatcherQueueController controller{ nullptr };

        QueueOwner()
        {
            DispatcherQueueOptions options{ sizeof(options), DQTYPE_THREAD_CURRENT, DQTAT_COM_NONE };
            winrt::check_hresult(CreateDispatcherQueueController(options,
                reinterpret_cast<ABI::Windows::System::IDispatcherQueueController**>(winrt::put_abi(controller))));
        }

        ~QueueOwner()
        {
            std::optional<int> quit;
            try
            {
                auto completionEvent = std::make_shared<winrt::handle>(CreateEventW(nullptr, TRUE, FALSE, nullptr));
                winrt::check_pointer(completionEvent->get());
                auto action = controller.ShutdownQueueAsync();
                action.Completed([completionEvent](auto const&, auto) {
                    if (!SetEvent(completionEvent->get())) LogHr("DispatcherQueue shutdown signal", HRESULT_FROM_WIN32(GetLastError()));
                });
                // The thread must keep pumping messages until ShutdownQueueAsync completes.
                HANDLE done = completionEvent->get();
                for (;;)
                {
                    DWORD result = MsgWaitForMultipleObjectsEx(1, &done, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
                    if (result == WAIT_OBJECT_0) break;
                    if (result == WAIT_FAILED) winrt::throw_last_error();
                    MSG message{};
                    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
                    {
                        if (message.message == WM_QUIT) quit = static_cast<int>(message.wParam);
                        else
                        {
                            TranslateMessage(&message);
                            DispatchMessageW(&message);
                        }
                        if (WaitForSingleObject(done, 0) == WAIT_OBJECT_0) break;
                    }
                }
                action.GetResults();
            }
            catch (...)
            {
                LogException("DispatcherQueue shutdown");
            }
            if (quit) PostQuitMessage(*quit);
        }
    };

    UiDispatcherScope::UiDispatcherScope()
    {
        static thread_local std::weak_ptr<QueueOwner> current;
        owner = current.lock();
        if (!owner && !DispatcherQueue::GetForCurrentThread())
        {
            owner = std::make_shared<QueueOwner>();
            current = owner;
        }
    }

    UiDispatcherScope::~UiDispatcherScope() = default;

    UiDispatch::UiDispatch() : queue(DispatcherQueue::GetForCurrentThread())
    {
        if (!queue) throw winrt::hresult_error(E_ILLEGAL_METHOD_CALL, L"No UI DispatcherQueue on this thread");
    }

    UiDispatch::~UiDispatch() { Close(); }

    void UiDispatch::Close() noexcept { open->store(false, std::memory_order_release); }

    bool UiDispatch::Post(std::function<void()> work) const
    {
        if (!open->load(std::memory_order_acquire)) return false;
        bool enqueued = queue.TryEnqueue([open = open, work = std::move(work)] {
            if (!open->load(std::memory_order_acquire)) return;
            try
            {
                work();
            }
            catch (...)
            {
                LogException("UI thread work");
            }
        });
        if (!enqueued && open->load(std::memory_order_acquire)) LogError("DispatcherQueue rejected UI work");
        return enqueued;
    }
}

namespace inkpages::detail
{
    bool UiThread::Post(std::function<void()> work)
    {
        return dispatch->Post(std::move(work));
    }
}
