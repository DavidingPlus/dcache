#include <gtest/gtest.h>

#include <fmt/args.h>
#include <fmt/format.h>
#include <fmt/ranges.h>

#include <iterator>
#include <string>
#include <vector>


TEST(FmtTests, FormatsCommonValueTypes)
{
    const auto result = fmt::format("name={}, count={}, enabled={}, ratio={}", "cache", 3, true, 0.75);

    EXPECT_EQ("name=cache, count=3, enabled=true, ratio=0.75", result);
}

TEST(FmtTests, SupportsNamedArgumentsAndFormatSpecifiers)
{
    const auto result = fmt::format(
        "{name:<10}|{id:#06x}|{load:.2f}",
        fmt::arg("name", "cache"),
        fmt::arg("id", 42),
        fmt::arg("load", 3.14159));

    EXPECT_EQ("cache     |0x002a|3.14", result);
}

TEST(FmtTests, FormatsAContainerWithJoin)
{
    const std::vector<int> replicas{1, 2, 3};

    EXPECT_EQ("replicas=[1, 2, 3]", fmt::format("replicas={}", replicas));
    EXPECT_EQ("1, 2, 3", fmt::format("{}", fmt::join(replicas, ", ")));
}

TEST(FmtTests, FormatsIntoAnOutputIterator)
{
    std::string output;

    fmt::format_to(std::back_inserter(output), "{}:{}", "key", 7);

    EXPECT_EQ("key:7", output);
}

TEST(FmtTests, ThrowsForInvalidRuntimeFormat)
{
    EXPECT_THROW(
        {
            static_cast<void>(fmt::format(fmt::runtime("value={"), 42));
        },
        fmt::format_error);
}
