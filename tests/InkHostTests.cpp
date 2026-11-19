// The ink thread's Run: the work's exception and a timeout reach the caller (StrokesOf must never hand back an empty
// page for a failed snapshot).
#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>

#include "ink/InkHost.h"

namespace inkpages::tests
{
    using detail::InkHost;

    TEST(InkHost, RunCarriesTheWorksExceptionAndItsTimeout)
    {
        winrt::init_apartment(winrt::apartment_type::single_threaded);
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
        winrt::uninit_apartment();
    }
}
