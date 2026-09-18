#include <gtest/gtest.h>

#include "cachegroup.h"

#include <atomic>
#include <chrono>
#include <future>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>


namespace
{

    constexpr auto kGetterStartTimeout = std::chrono::seconds(1);
    constexpr auto kFollowerWaitTimeout = std::chrono::milliseconds(50);


    void ExpectValue(const ByteViewOptional &actual, const std::string &expected)
    {
        ASSERT_TRUE(actual.has_value());
        EXPECT_EQ(expected, actual->toString());
    }


    DataGetter MakeGetter(std::atomic<int> &callCount, std::string prefix = "loaded:")
    {
        return [&callCount, prefix = std::move(prefix)](const std::string &key) -> ByteViewOptional
        {
            ++callCount;
            return ByteView(prefix + key);
        };
    }

} // namespace


TEST(DCacheGroupTests, GetLoadsMissingKeyAndCachesLoadedValue)
{
    std::atomic<int> callCount{0};
    DCacheGroup group("users", 0, MakeGetter(callCount));

    ExpectValue(group.get("user-1"), "loaded:user-1");
    ExpectValue(group.get("user-1"), "loaded:user-1");

    EXPECT_EQ(1, callCount.load());
}


TEST(DCacheGroupTests, GetDoesNotCacheMissingValues)
{
    std::atomic<int> callCount{0};
    DCacheGroup group(
        "users",
        0,
        [&callCount](const std::string &) -> ByteViewOptional
        {
            ++callCount;
            return std::nullopt;
        });

    EXPECT_FALSE(group.get("missing").has_value());
    EXPECT_FALSE(group.get("missing").has_value());
    EXPECT_EQ(2, callCount.load());
}


TEST(DCacheGroupTests, GetRejectsEmptyKeyWithoutCallingGetter)
{
    std::atomic<int> callCount{0};
    DCacheGroup group("users", 0, MakeGetter(callCount));

    EXPECT_FALSE(group.get("").has_value());
    EXPECT_EQ(0, callCount.load());
}


TEST(DCacheGroupTests, GetPreservesEmptyAndBinaryValues)
{
    std::atomic<int> callCount{0};
    const std::string binary{std::string{'a', '\0', 'b'}};
    DCacheGroup group(
        "values",
        0,
        [&callCount, binary](const std::string &key) -> ByteViewOptional
        {
            ++callCount;
            if (key == "empty") return ByteView("");
            return ByteView(binary);
        });

    const auto empty = group.get("empty");
    const auto binaryValue = group.get("binary");

    ASSERT_TRUE(empty.has_value());
    EXPECT_EQ("", empty->toString());
    ASSERT_TRUE(binaryValue.has_value());
    EXPECT_EQ(binary, binaryValue->toString());
    EXPECT_EQ(2, callCount.load());

    ExpectValue(group.get("empty"), "");
    EXPECT_EQ(2, callCount.load());
}


TEST(DCacheGroupTests, SetStoresValueAndOverridesGetter)
{
    std::atomic<int> callCount{0};
    DCacheGroup group("users", 0, MakeGetter(callCount));

    ASSERT_TRUE(group.set("user-1", ByteView("from-set")));

    ExpectValue(group.get("user-1"), "from-set");
    EXPECT_EQ(0, callCount.load());
}


TEST(DCacheGroupTests, SetRejectsEmptyKey)
{
    std::atomic<int> callCount{0};
    DCacheGroup group("users", 0, MakeGetter(callCount));

    EXPECT_FALSE(group.set("", ByteView("value")));
    EXPECT_FALSE(group.get("").has_value());
    EXPECT_EQ(0, callCount.load());
}


TEST(DCacheGroupTests, DeleteByKeyRemovesLocalValue)
{
    std::atomic<int> callCount{0};
    DCacheGroup group("users", 0, MakeGetter(callCount));

    ASSERT_TRUE(group.set("user-1", ByteView("cached")));
    ASSERT_TRUE(group.deleteByKey("user-1"));

    ExpectValue(group.get("user-1"), "loaded:user-1");
    EXPECT_EQ(1, callCount.load());
}


TEST(DCacheGroupTests, DeleteByKeyAcceptsMissingKey)
{
    std::atomic<int> callCount{0};
    DCacheGroup group("users", 0, MakeGetter(callCount));

    EXPECT_TRUE(group.deleteByKey("missing"));
    EXPECT_EQ(0, callCount.load());
}


TEST(DCacheGroupTests, DeleteByKeyRejectsEmptyKey)
{
    std::atomic<int> callCount{0};
    DCacheGroup group("users", 0, MakeGetter(callCount));

    EXPECT_FALSE(group.deleteByKey(""));
    EXPECT_EQ(0, callCount.load());
}


TEST(DCacheGroupTests, InvalidateFromPeerRemovesLocalValueWithoutLoading)
{
    std::atomic<int> callCount{0};
    DCacheGroup group("users", 0, MakeGetter(callCount));

    ASSERT_TRUE(group.set("user-1", ByteView("cached")));
    ASSERT_TRUE(group.invalidateFromPeer("user-1"));
    EXPECT_EQ(0, callCount.load());

    ExpectValue(group.get("user-1"), "loaded:user-1");
    EXPECT_EQ(1, callCount.load());
}


TEST(DCacheGroupTests, InvalidateFromPeerAcceptsMissingKey)
{
    std::atomic<int> callCount{0};
    DCacheGroup group("users", 0, MakeGetter(callCount));

    EXPECT_TRUE(group.invalidateFromPeer("missing"));
    EXPECT_EQ(0, callCount.load());
}


TEST(DCacheGroupTests, InvalidateFromPeerRejectsEmptyKey)
{
    std::atomic<int> callCount{0};
    DCacheGroup group("users", 0, MakeGetter(callCount));

    EXPECT_FALSE(group.invalidateFromPeer(""));
    EXPECT_EQ(0, callCount.load());
}


TEST(DCacheGroupTests, GetterExceptionIsPropagatedAndKeyCanBeRetried)
{
    std::atomic<int> callCount{0};
    DCacheGroup group(
        "users",
        0,
        [&callCount](const std::string &) -> ByteViewOptional
        {
            const auto call = ++callCount;
            if (call == 1) throw std::runtime_error("getter failed");
            return ByteView("recovered");
        });

    EXPECT_THROW(group.get("user-1"), std::runtime_error);
    ExpectValue(group.get("user-1"), "recovered");
    EXPECT_EQ(2, callCount.load());
}


TEST(DCacheGroupTests, ConcurrentGetsShareOneGetterCall)
{
    std::atomic<int> callCount{0};
    std::promise<void> getterStarted;
    auto getterStartedFuture = getterStarted.get_future();
    std::promise<void> allowGetterToFinish;
    const auto allowGetterToFinishFuture = allowGetterToFinish.get_future().share();

    DCacheGroup group(
        "users",
        0,
        [&callCount, &getterStarted, allowGetterToFinishFuture](const std::string &) -> ByteViewOptional
        {
            const auto call = ++callCount;
            if (call == 1)
            {
                getterStarted.set_value();
                allowGetterToFinishFuture.wait();
            }
            return ByteView("shared-value");
        });

    auto leader = std::async(std::launch::async, [&group]
                             { return group.get("hot-key"); });

    if (getterStartedFuture.wait_for(kGetterStartTimeout) != std::future_status::ready)
    {
        allowGetterToFinish.set_value();
        FAIL() << "the getter did not start";
    }

    std::vector<std::future<ByteViewOptional>> followers;
    for (int i = 0; i < 4; ++i)
    {
        followers.emplace_back(std::async(std::launch::async, [&group]
                                          { return group.get("hot-key"); }));
    }

    bool allFollowersWaiting = true;
    for (auto &follower : followers)
    {
        allFollowersWaiting =
            allFollowersWaiting && follower.wait_for(kFollowerWaitTimeout) == std::future_status::timeout;
    }

    allowGetterToFinish.set_value();

    EXPECT_TRUE(allFollowersWaiting);
    ExpectValue(leader.get(), "shared-value");
    for (auto &follower : followers) ExpectValue(follower.get(), "shared-value");
    EXPECT_EQ(1, callCount.load());
}


TEST(DCacheGroupTests, CapacityIsForwardedToLocalCache)
{
    std::atomic<int> callCount{0};
    DCacheGroup group("users", 6, MakeGetter(callCount));

    ASSERT_TRUE(group.set("a", ByteView("1")));
    ASSERT_TRUE(group.set("b", ByteView("2")));
    ASSERT_TRUE(group.set("c", ByteView("3")));
    ASSERT_TRUE(group.set("d", ByteView("4")));

    // 每个 key/value 对占用 2 字节，写入 d 后最早写入的 a 应被淘汰。
    ExpectValue(group.get("a"), "loaded:a");
    EXPECT_EQ(1, callCount.load());
}


TEST(DCacheGroupTests, MoveConstructorTransfersCacheAndGetter)
{
    std::atomic<int> callCount{0};
    DCacheGroup source("users", 0, MakeGetter(callCount));
    ASSERT_TRUE(source.set("cached", ByteView("cached-value")));

    DCacheGroup moved(std::move(source));

    ExpectValue(moved.get("cached"), "cached-value");
    ExpectValue(moved.get("loaded"), "loaded:loaded");
    EXPECT_EQ(1, callCount.load());
}


TEST(DCacheGroupTests, MoveAssignmentReplacesDestinationState)
{
    std::atomic<int> sourceCalls{0};
    std::atomic<int> destinationCalls{0};
    DCacheGroup source("source", 0, MakeGetter(sourceCalls, "source:"));
    DCacheGroup destination("destination", 0, MakeGetter(destinationCalls, "destination:"));

    ASSERT_TRUE(source.set("source-key", ByteView("source-value")));
    ASSERT_TRUE(destination.set("destination-key", ByteView("destination-value")));

    DCacheGroup &assigned = (destination = std::move(source));

    EXPECT_EQ(&destination, &assigned);
    ExpectValue(destination.get("source-key"), "source-value");
    ExpectValue(destination.get("destination-key"), "source:destination-key");
    EXPECT_EQ(1, sourceCalls.load());
    EXPECT_EQ(0, destinationCalls.load());
}
