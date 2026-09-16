#include <gtest/gtest.h>

#include "singleflight.h"

#include <atomic>
#include <chrono>
#include <future>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>


namespace
{

    constexpr auto kWorkerStartTimeout = std::chrono::seconds(1);
    constexpr auto kFollowerWaitTimeout = std::chrono::milliseconds(50);


    void ExpectValue(const ByteViewOptional &result, const std::string &expected)
    {
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(expected, result->toString());
    }

} // namespace


TEST(SingleFlightTests, SameKeyConcurrentCallsExecuteFuncOnceAndShareValue)
{
    SingleFlight singleFlight;
    std::atomic<int> callCount{0};
    std::atomic<bool> followerFuncExecuted{false};
    std::promise<void> funcStarted;
    std::future<void> funcStartedFuture = funcStarted.get_future();
    std::promise<void> allowFuncToFinish;
    const std::shared_future<void> allowFuncToFinishFuture = allowFuncToFinish.get_future().share();

    auto leader = std::async(std::launch::async, [&]()
                             { return singleFlight.Do("key", [&]() -> ByteViewOptional
                                                      {
                                                            ++callCount;
                                                            funcStarted.set_value();
                                                            allowFuncToFinishFuture.wait();
                                                            return ByteView("value"); }); });

    EXPECT_EQ(std::future_status::ready, funcStartedFuture.wait_for(kWorkerStartTimeout));

    std::vector<std::future<ByteViewOptional>> followers;
    for (int i = 0; i < 4; ++i)
    {
        followers.emplace_back(std::async(std::launch::async, [&]()
                                          { return singleFlight.Do("key", [&]() -> ByteViewOptional
                                                                   {
                                                                         followerFuncExecuted = true;
                                                                         return ByteView("unexpected"); }); }));
    }

    // leader 尚未完成时，所有 follower 都应等待同一个 Call，而不是执行自己的 func。
    for (auto &follower : followers)
    {
        EXPECT_EQ(std::future_status::timeout, follower.wait_for(kFollowerWaitTimeout));
    }

    allowFuncToFinish.set_value();

    ExpectValue(leader.get(), "value");
    for (auto &follower : followers)
    {
        ExpectValue(follower.get(), "value");
    }

    EXPECT_EQ(1, callCount.load());
    EXPECT_FALSE(followerFuncExecuted.load());
}


TEST(SingleFlightTests, DifferentKeysCanExecuteFuncsInParallel)
{
    SingleFlight singleFlight;
    std::promise<void> firstFuncStarted;
    std::future<void> firstFuncStartedFuture = firstFuncStarted.get_future();
    std::promise<void> secondFuncStarted;
    std::future<void> secondFuncStartedFuture = secondFuncStarted.get_future();
    std::promise<void> allowFuncsToFinish;
    const std::shared_future<void> allowFuncsToFinishFuture = allowFuncsToFinish.get_future().share();

    auto first = std::async(std::launch::async, [&]()
                            { return singleFlight.Do("first", [&]() -> ByteViewOptional
                                                     {
                                                           firstFuncStarted.set_value();
                                                           allowFuncsToFinishFuture.wait();
                                                           return ByteView("first-value"); }); });
    auto second = std::async(std::launch::async, [&]()
                             { return singleFlight.Do("second", [&]() -> ByteViewOptional
                                                      {
                                                            secondFuncStarted.set_value();
                                                            allowFuncsToFinishFuture.wait();
                                                            return ByteView("second-value"); }); });

    // 两个函数均在 release 前开始，证明组锁没有覆盖耗时的 func 执行。
    EXPECT_EQ(std::future_status::ready, firstFuncStartedFuture.wait_for(kWorkerStartTimeout));
    EXPECT_EQ(std::future_status::ready, secondFuncStartedFuture.wait_for(kWorkerStartTimeout));

    allowFuncsToFinish.set_value();

    ExpectValue(first.get(), "first-value");
    ExpectValue(second.get(), "second-value");
}


TEST(SingleFlightTests, SameKeyConcurrentCallsShareEmptyResult)
{
    SingleFlight singleFlight;
    std::atomic<int> callCount{0};
    std::atomic<bool> followerFuncExecuted{false};
    std::promise<void> funcStarted;
    std::future<void> funcStartedFuture = funcStarted.get_future();
    std::promise<void> allowFuncToFinish;
    const std::shared_future<void> allowFuncToFinishFuture = allowFuncToFinish.get_future().share();

    auto leader = std::async(std::launch::async, [&]()
                             { return singleFlight.Do("missing", [&]() -> ByteViewOptional
                                                      {
                                                            ++callCount;
                                                            funcStarted.set_value();
                                                            allowFuncToFinishFuture.wait();
                                                            return std::nullopt; }); });

    EXPECT_EQ(std::future_status::ready, funcStartedFuture.wait_for(kWorkerStartTimeout));

    auto follower = std::async(std::launch::async, [&]()
                               { return singleFlight.Do("missing", [&]() -> ByteViewOptional
                                                        {
                                                              followerFuncExecuted = true;
                                                              return ByteView("unexpected"); }); });

    EXPECT_EQ(std::future_status::timeout, follower.wait_for(kFollowerWaitTimeout));
    allowFuncToFinish.set_value();

    EXPECT_FALSE(leader.get().has_value());
    EXPECT_FALSE(follower.get().has_value());
    EXPECT_EQ(1, callCount.load());
    EXPECT_FALSE(followerFuncExecuted.load());
}


TEST(SingleFlightTests, CompletedCallIsNotUsedAsAResultCache)
{
    SingleFlight singleFlight;
    int callCount = 0;

    const auto first = singleFlight.Do("key", [&]() -> ByteViewOptional
                                       {
                                           ++callCount;
                                           return ByteView("first"); });
    const auto second = singleFlight.Do("key", [&]() -> ByteViewOptional
                                        {
                                            ++callCount;
                                            return ByteView("second"); });

    ExpectValue(first, "first");
    ExpectValue(second, "second");
    EXPECT_EQ(2, callCount);
}


TEST(SingleFlightTests, ConcurrentFollowersReceiveLeaderException)
{
    SingleFlight singleFlight;
    std::atomic<int> callCount{0};
    std::atomic<bool> followerFuncExecuted{false};
    std::promise<void> funcStarted;
    std::future<void> funcStartedFuture = funcStarted.get_future();
    std::promise<void> allowFuncToFail;
    const std::shared_future<void> allowFuncToFailFuture = allowFuncToFail.get_future().share();

    auto leader = std::async(std::launch::async, [&]()
                             { return singleFlight.Do("key", [&]() -> ByteViewOptional
                                                      {
                                                            ++callCount;
                                                            funcStarted.set_value();
                                                            allowFuncToFailFuture.wait();
                                                            throw std::runtime_error("load failed"); }); });

    EXPECT_EQ(std::future_status::ready, funcStartedFuture.wait_for(kWorkerStartTimeout));

    auto follower = std::async(std::launch::async, [&]()
                               { return singleFlight.Do("key", [&]() -> ByteViewOptional
                                                        {
                                                              followerFuncExecuted = true;
                                                              return ByteView("unexpected"); }); });

    EXPECT_EQ(std::future_status::timeout, follower.wait_for(kFollowerWaitTimeout));
    allowFuncToFail.set_value();

    EXPECT_THROW(leader.get(), std::runtime_error);
    EXPECT_THROW(follower.get(), std::runtime_error);
    EXPECT_EQ(1, callCount.load());
    EXPECT_FALSE(followerFuncExecuted.load());
}


TEST(SingleFlightTests, FailedCallIsRemovedSoTheKeyCanBeRetried)
{
    SingleFlight singleFlight;
    int callCount = 0;

    EXPECT_THROW(
        singleFlight.Do("key", [&]() -> ByteViewOptional
                        {
                            ++callCount;
                            throw std::runtime_error("load failed"); //
                        }),
        std::runtime_error);

    const auto result = singleFlight.Do("key", [&]() -> ByteViewOptional
                                        {
                                            ++callCount;
                                            return ByteView("value"); //
                                        });

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ("value", result->toString());
    EXPECT_EQ(2, callCount);
}
