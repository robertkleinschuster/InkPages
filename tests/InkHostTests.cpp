// The ink thread's Run: the work's exception and a timeout reach the caller (StrokesOf must never hand back an empty
// page for a failed snapshot).
#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>

#include "ink/InkHost.h"

namespace inkpages::tests
{
    using detail::InkHost;

    // The apartment the host is created in; it is uninitialized after the host is gone (as in an app: COM unloads the
    // ink DLL when the apartment ends - a host released after that runs in an unloaded DLL).
    struct Apartment
    {
        Apartment() { winrt::init_apartment(winrt::apartment_type::single_threaded); }
        ~Apartment() { winrt::uninit_apartment(); }
    };

    TEST(InkHost, RunCarriesTheWorksExceptionAndItsTimeout)
    {
        Apartment apartment;
        InkHost host;
        int value = 0;
        host.Run("value", [&] { value = 42; });
        EXPECT_EQ(42, value);
        EXPECT_THROW(host.Run("throws", [] { throw std::runtime_error("broken"); }), std::runtime_error);
        auto release = std::make_shared<HANDLE>(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        try
        {
            host.Run("slow", [release] { WaitForSingleObject(*release, 2000); }, 50);
            FAIL() << "a timeout throws";
        }
        catch (winrt::hresult_error const& e)
        {
            EXPECT_EQ(HRESULT_FROM_WIN32(ERROR_TIMEOUT), e.code());
        }
        SetEvent(*release);
        host.Run("after", [&] { value = 7; }, INFINITE); // the thread goes on after the slow work
        EXPECT_EQ(7, value);
        CloseHandle(*release);
    }

    // Windows ends the ink thread asynchronously after the host's last release, and CoUninitialize of the creating
    // apartment unloads Windows.UI.Input.Inking.dll, whose code the thread runs: the host must wait for the thread's end
    // (otherwise the thread crashes in the unloaded DLL: an access violation on the ink thread, nearly every round).
    TEST(InkHost, TheInkThreadHasEndedWhenTheHostIsGone)
    {
        for (int round = 0; round < 5; ++round)
        {
            Apartment apartment; // uninitialized at once after the host, as an app's last step
            InkHost host;
            host.Run("work", [] {});
        }
        Sleep(200); // a thread left behind in the unloaded DLL crashes within this time
    }
}
