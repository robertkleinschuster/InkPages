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
            Apartment apartment;
            InkHost host;
            host.Run("work", [] {});
        }
        Sleep(200); // a thread left behind in the unloaded DLL crashes within this time
    }

    TEST(InkHost, DestructionWaitsForOutstandingWork)
    {
        Apartment apartment;
        winrt::handle thread;
        winrt::handle started(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        ASSERT_TRUE(static_cast<bool>(started));
        {
            InkHost host;
            host.Run("capture thread handle", [&] {
                winrt::check_bool(DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(),
                    thread.put(), SYNCHRONIZE, FALSE, 0));
            });
            host.Post("outstanding work", [started = started.get()] {
                winrt::check_bool(SetEvent(started));
                Sleep(5500); // exceeds the former shutdown timeout
            });
            ASSERT_EQ(WAIT_OBJECT_0, WaitForSingleObject(started.get(), 2000));
        }
        EXPECT_EQ(WAIT_OBJECT_0, WaitForSingleObject(thread.get(), 0));
        // Keep the apartment alive for the thread even when running this regression against the old implementation.
        EXPECT_EQ(WAIT_OBJECT_0, WaitForSingleObject(thread.get(), 10000));
    }
}
