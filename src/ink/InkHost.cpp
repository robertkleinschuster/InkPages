#include "ink/InkHost.h"

#include <format>
#include <memory>

#include "inkpages/Log.h"

namespace inkpages::detail
{
    namespace
    {
        struct WorkItem : winrt::implements<WorkItem, IInkHostWorkItem>
        {
            WorkItem(char const* name, std::function<void()> work) : name(name), work(std::move(work)) {}
            HRESULT STDMETHODCALLTYPE Invoke() noexcept override
            {
                try
                {
                    work();
                }
                catch (...)
                {
                    LogException(std::format("ink thread: {}", name));
                }
                return S_OK;
            }
            char const* name;
            std::function<void()> work;
        };

        struct CommitHandler : winrt::implements<CommitHandler, IInkCommitRequestHandler>
        {
            explicit CommitHandler(std::function<void()> commit) : commit(std::move(commit)) {}
            HRESULT STDMETHODCALLTYPE OnCommitRequested() noexcept override
            {
                try
                {
                    commit();
                }
                catch (...)
                {
                    LogException("ink: commit request");
                }
                return S_OK;
            }
            std::function<void()> commit;
        };

        struct Event
        {
            Event() : handle(CreateEventW(nullptr, TRUE, FALSE, nullptr)) { winrt::check_pointer(handle); }
            ~Event() { CloseHandle(handle); }
            HANDLE handle;
        };
    }

    InkHost::InkHost()
    {
        host = winrt::create_instance<IInkDesktopHost>(__uuidof(InkDesktopHost), CLSCTX_INPROC_SERVER);
        Post("thread", [this] { thread = GetCurrentThreadId(); });
    }

    void InkHost::Post(char const* name, std::function<void()> work)
    {
        if (OnThread())
        {
            WorkItem(name, std::move(work)).Invoke();
            return;
        }
        HRESULT hr = host->QueueWorkItem(winrt::make<WorkItem>(name, std::move(work)).get());
        if (FAILED(hr)) LogHr(std::format("QueueWorkItem({})", name), hr);
    }

    bool InkHost::Run(char const* name, std::function<void()> work, DWORD timeoutMs)
    {
        if (OnThread())
        {
            WorkItem(name, std::move(work)).Invoke();
            return true;
        }
        auto done = std::make_shared<Event>();
        Post(name, [work = std::move(work), done] {
            work();
            SetEvent(done->handle);
        });
        if (WaitForSingleObject(done->handle, timeoutMs) == WAIT_OBJECT_0) return true;
        LogError(std::format("ink: {} did not finish within {} ms", name, timeoutMs));
        return false;
    }

    winrt::com_ptr<IInkCommitRequestHandler> MakeCommitHandler(std::function<void()> commit)
    {
        return winrt::make<CommitHandler>(std::move(commit)).as<IInkCommitRequestHandler>();
    }
}
