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
        struct ThreadInfo
        {
            winrt::handle handle;
            DWORD id = 0;
        };
        auto info = std::make_shared<ThreadInfo>();
        Run("thread", [info] {
            if (!DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(),
                info->handle.put(), SYNCHRONIZE, FALSE, 0))
            {
                winrt::throw_hresult(LogHr("ink: opening the ink thread", HRESULT_FROM_WIN32(GetLastError())));
            }
            info->id = GetCurrentThreadId();
        });
        threadHandle = std::move(info->handle);
        thread = info->id;
    }

    InkHost::~InkHost()
    {
        if (OnThread())
        {
            LogError("ink: the ink host is destroyed on its own thread - its end cannot be waited for");
            return;
        }
        host = nullptr; // the last reference: Windows ends the ink thread
        if (!threadHandle) return;
        // Measured (2026-11-20): the thread ends within milliseconds of the release; COM unloads its DLL when the
        // creating apartment uninitializes, so it must have ended before that.
        HANDLE handle = threadHandle.get();
        DWORD index = 0;
        HRESULT hr = CoWaitForMultipleHandles(COWAIT_DEFAULT, INFINITE, 1, &handle, &index);
        if (FAILED(hr))
        {
            LogHr("ink: COM wait for the ink thread", hr);
            if (WaitForSingleObject(handle, INFINITE) == WAIT_FAILED)
                LogHr("ink: waiting for the ink thread's end", HRESULT_FROM_WIN32(GetLastError()));
        }
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

    void InkHost::Run(char const* name, std::function<void()> work, DWORD timeoutMs)
    {
        if (OnThread())
        {
            work();
            return;
        }
        struct Done : Event
        {
            std::exception_ptr error;
        };
        auto done = std::make_shared<Done>();
        HRESULT hr = host->QueueWorkItem(winrt::make<WorkItem>(name, [work = std::move(work), done] {
            // The event is set however the work ends; its exception goes to the waiting caller.
            struct Signal
            {
                HANDLE handle;
                ~Signal() { SetEvent(handle); }
            } signal{ done->handle };
            try
            {
                work();
            }
            catch (...)
            {
                done->error = std::current_exception();
            }
        }).get());
        if (FAILED(hr)) winrt::throw_hresult(LogHr(std::format("QueueWorkItem({})", name), hr));
        if (WaitForSingleObject(done->handle, timeoutMs) != WAIT_OBJECT_0)
        {
            LogError(std::format("ink: {} did not finish within {} ms", name, timeoutMs));
            throw winrt::hresult_error(HRESULT_FROM_WIN32(ERROR_TIMEOUT), winrt::to_hstring(std::format("ink thread: {} timed out", name)));
        }
        if (done->error) std::rethrow_exception(done->error);
    }

    winrt::com_ptr<IInkCommitRequestHandler> MakeCommitHandler(std::function<void()> commit)
    {
        return winrt::make<CommitHandler>(std::move(commit)).as<IInkCommitRequestHandler>();
    }
}
