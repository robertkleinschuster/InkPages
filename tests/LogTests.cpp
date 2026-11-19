#include <gtest/gtest.h>

#include <winrt/base.h>

#include <stdexcept>
#include <string>
#include <vector>

#include "inkpages/Log.h"

namespace inkpages::tests
{
    TEST(Log, DescribeHrGivesTheCodeAndTheSystemsText)
    {
        std::string text = DescribeHr(E_ACCESSDENIED);
        EXPECT_EQ("0x80070005", text.substr(0, 10));
        EXPECT_GT(text.size(), 11u) << "the system's message follows the code";
    }

    TEST(Log, DescribeHrOfAnUnknownCodeIsTheNumber)
    {
        EXPECT_EQ("0x8FFFFFFF", DescribeHr(static_cast<HRESULT>(0x8FFFFFFF)));
    }

    TEST(Log, ErrorsReachTheErrorSinkPlainLinesDoNot)
    {
        std::vector<std::string> errors;
        SetErrorSink([&](std::string_view line) { errors.emplace_back(line); });
        Log("a plain line");
        LogHr("a step that worked", S_OK);
        LogHr("a step that failed", E_FAIL);
        try
        {
            throw winrt::hresult_error(E_INVALIDARG, L"bad argument");
        }
        catch (...)
        {
            LogException("winrt call");
        }
        try
        {
            throw std::runtime_error("broken");
        }
        catch (...)
        {
            LogException("std call");
        }
        SetErrorSink(nullptr);

        ASSERT_EQ(3u, errors.size());
        EXPECT_NE(std::string::npos, errors[0].find("a step that failed: 0x80004005"));
        EXPECT_NE(std::string::npos, errors[1].find("winrt call: winrt::hresult_error 0x80070057"));
        EXPECT_NE(std::string::npos, errors[1].find("bad argument"));
        EXPECT_NE(std::string::npos, errors[2].find("std call:"));
        EXPECT_NE(std::string::npos, errors[2].find("broken"));
    }
}
