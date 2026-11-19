#include "inkpages/Log.h"

#include <winrt/base.h>

#include <chrono>
#include <cstdio>
#include <format>
#include <mutex>
#include <typeinfo>

namespace inkpages
{
    namespace
    {
        std::mutex lock;
        std::wstring file;
        std::function<void(std::string_view)> errorSink;

        void Write(std::string_view text, bool error)
        {
            auto now = std::chrono::system_clock::now();
            auto local = std::chrono::current_zone()->to_local(now);
            std::string line = std::format("{:%H:%M:%S} {} {}{}\n",
                std::chrono::floor<std::chrono::milliseconds>(local), GetCurrentThreadId(), error ? "ERROR " : "", text);
            std::function<void(std::string_view)> sink;
            {
                std::scoped_lock guard(lock);
                if (!file.empty())
                {
                    FILE* f = nullptr;
                    if (_wfopen_s(&f, file.c_str(), L"ab") == 0 && f)
                    {
                        fwrite(line.data(), 1, line.size(), f);
                        fclose(f);
                    }
                }
                sink = errorSink;
            }
            if (error && sink)
            {
                sink(line);
            }
        }
    }

    void SetLogFile(std::wstring path)
    {
        std::scoped_lock guard(lock);
        file = std::move(path);
    }

    void SetErrorSink(std::function<void(std::string_view line)> sink)
    {
        std::scoped_lock guard(lock);
        errorSink = std::move(sink);
    }

    void Log(std::string_view text) { Write(text, false); }

    void LogError(std::string_view text) { Write(text, true); }

    std::string DescribeHr(HRESULT hr)
    {
        wchar_t* buffer = nullptr;
        DWORD length = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, static_cast<DWORD>(hr), 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
        std::string text;
        if (length > 0 && buffer)
        {
            std::wstring message(buffer, length);
            LocalFree(buffer);
            while (!message.empty() && (message.back() == L'\n' || message.back() == L'\r' || message.back() == L' '))
            {
                message.pop_back();
            }
            text = winrt::to_string(message);
        }
        return text.empty() ? std::format("0x{:08X}", static_cast<unsigned>(hr))
                            : std::format("0x{:08X} {}", static_cast<unsigned>(hr), text);
    }

    HRESULT LogHr(std::string_view step, HRESULT hr)
    {
        if (FAILED(hr))
        {
            LogError(std::format("{}: {}", step, DescribeHr(hr)));
        }
        else
        {
            Log(std::format("{}: ok", step));
        }
        return hr;
    }

    void LogException(std::string_view context, std::exception_ptr error)
    {
        try
        {
            std::rethrow_exception(error);
        }
        catch (winrt::hresult_error const& e)
        {
            LogError(std::format("{}: winrt::hresult_error {} {}", context,
                DescribeHr(e.code()), winrt::to_string(e.message())));
        }
        catch (std::exception const& e)
        {
            LogError(std::format("{}: {} {}", context, typeid(e).name(), e.what()));
        }
        catch (...)
        {
            LogError(std::format("{}: unknown exception", context));
        }
    }
}
