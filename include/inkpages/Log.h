#pragma once

// The library's log (rule: never swallow an error – log its kind, HRESULT and message). One line per call, with the
// time and thread. The app chooses where lines go (SetLogFile); errors also go to an app callback (SetErrorSink),
// e.g. for an errors.log that is always written.

#include <windows.h>

#include <exception>
#include <functional>
#include <string>
#include <string_view>

namespace inkpages
{
    /// Lines go to this file (UTF-8, appended); empty: nowhere (the default).
    void SetLogFile(std::wstring path);

    /// Errors (LogError, LogHr with a failure, LogException) are also handed to this callback (null: none).
    void SetErrorSink(std::function<void(std::string_view line)> sink);

    void Log(std::string_view text);

    void LogError(std::string_view text);

    /// "0x80070005 Zugriff verweigert" – the HRESULT and the system's text for it (no text: only the number).
    std::string DescribeHr(HRESULT hr);

    /// Logs a step's HRESULT: a failure as an error, success as a plain line. Returns hr (to check it on).
    HRESULT LogHr(std::string_view step, HRESULT hr);

    /// Logs the exception in flight (call inside a catch): winrt::hresult_error with its HRESULT and message,
    /// std::exception with its type and what(), anything else as unknown.
    void LogException(std::string_view context, std::exception_ptr error = std::current_exception());
}
