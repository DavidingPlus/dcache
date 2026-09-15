#include <gtest/gtest.h>

#include "lrucache.h"

#include <future>
#include <initializer_list>
#include <string>
#include <thread>
#include <utility>
#include <vector>


namespace
{

    using EvictedEntries = std::vector<std::pair<std::string, std::string>>;


    void ExpectValue(const ByteViewOptional &actual, const std::string &expected)
    {
        ASSERT_TRUE(actual.has_value());
        EXPECT_EQ(expected, actual->toString());
    }

    void ExpectMissing(const ByteViewOptional &actual)
    {
        EXPECT_FALSE(actual.has_value());
    }

    void ExpectEvicted(const EvictedEntries &actual, std::initializer_list<std::pair<std::string, std::string>> expected)
    {
        ASSERT_EQ(expected.size(), actual.size());

        auto expectedIter = expected.begin();
        for (auto actualIter = actual.begin(); actualIter != actual.end(); ++actualIter, ++expectedIter)
        {
            EXPECT_EQ(expectedIter->first, actualIter->first);
            EXPECT_EQ(expectedIter->second, actualIter->second);
        }
    }

} // namespace


TEST(ByteViewTests, PreservesEmptyAndBinaryData)
{
    const std::string binary{std::string{'a', '\0', 'b'}};

    const ByteView empty("");
    EXPECT_EQ(0, empty.len());
    EXPECT_EQ("", empty.toString());

    const ByteView bytes(binary);
    EXPECT_EQ(3, bytes.len());
    EXPECT_EQ(binary, bytes.toString());
}

TEST(EntryTests, ComparesKeyAndValue)
{
    const Entry first("key", ByteView("value"));
    const Entry same("key", ByteView("value"));
    const Entry differentValue("key", ByteView("other"));
    const Entry differentKey("other", ByteView("value"));

    EXPECT_TRUE(first == same);
    EXPECT_FALSE(first == differentValue);
    EXPECT_FALSE(first == differentKey);
}

TEST(LRUCacheTests, GetReturnsNulloptForMissingKey)
{
    LRUCache cache(0);

    ExpectMissing(cache.get("missing"));
}

TEST(LRUCacheTests, SetStoresEmptyAndBinaryValues)
{
    LRUCache cache(0);
    const std::string binary{std::string{'a', '\0', 'b'}};

    cache.set("empty", ByteView(""));
    cache.set("binary", ByteView(binary));

    ExpectValue(cache.get("empty"), "");
    ExpectValue(cache.get("binary"), binary);
}

TEST(LRUCacheTests, SetExistingKeyReplacesValueWithoutEviction)
{
    EvictedEntries evicted;
    LRUCache cache(8, [&evicted](std::string key, ByteView value)
                   { evicted.emplace_back(std::move(key), value.toString()); });

    cache.set("key", ByteView("old"));
    cache.set("key", ByteView("value"));

    ExpectValue(cache.get("key"), "value");
    EXPECT_TRUE(evicted.empty());
}

TEST(LRUCacheTests, SetExistingKeyRefreshesRecency)
{
    EvictedEntries evicted;
    LRUCache cache(6, [&evicted](std::string key, ByteView value)
                   { evicted.emplace_back(std::move(key), value.toString()); });

    cache.set("a", ByteView("1"));
    cache.set("b", ByteView("2"));
    cache.set("c", ByteView("3"));
    cache.set("a", ByteView("4"));
    cache.set("d", ByteView("5"));

    ExpectEvicted(evicted, {{"b", "2"}});
    ExpectValue(cache.get("a"), "4");
    ExpectValue(cache.get("c"), "3");
    ExpectValue(cache.get("d"), "5");
    ExpectMissing(cache.get("b"));
}

TEST(LRUCacheTests, GetRefreshesRecency)
{
    EvictedEntries evicted;
    LRUCache cache(6, [&evicted](std::string key, ByteView value)
                   { evicted.emplace_back(std::move(key), value.toString()); });

    cache.set("a", ByteView("1"));
    cache.set("b", ByteView("2"));
    cache.set("c", ByteView("3"));
    ExpectValue(cache.get("a"), "1");
    cache.set("d", ByteView("4"));

    ExpectEvicted(evicted, {{"b", "2"}});
    ExpectValue(cache.get("a"), "1");
    ExpectValue(cache.get("c"), "3");
    ExpectValue(cache.get("d"), "4");
    ExpectMissing(cache.get("b"));
}

TEST(LRUCacheTests, GetMissDoesNotChangeRecency)
{
    EvictedEntries evicted;
    LRUCache cache(6, [&evicted](std::string key, ByteView value)
                   { evicted.emplace_back(std::move(key), value.toString()); });

    cache.set("a", ByteView("1"));
    cache.set("b", ByteView("2"));
    cache.set("c", ByteView("3"));
    ExpectMissing(cache.get("missing"));
    cache.set("d", ByteView("4"));

    ExpectEvicted(evicted, {{"a", "1"}});
    ExpectMissing(cache.get("a"));
    ExpectValue(cache.get("b"), "2");
    ExpectValue(cache.get("c"), "3");
    ExpectValue(cache.get("d"), "4");
}

TEST(LRUCacheTests, SetEvictsOldestEntryWhenCapacityIsExceeded)
{
    EvictedEntries evicted;
    LRUCache cache(6, [&evicted](std::string key, ByteView value)
                   {
                       evicted.emplace_back(std::move(key), value.toString()); //
                   });

    cache.set("a", ByteView("1"));
    cache.set("b", ByteView("2"));
    cache.set("c", ByteView("3"));
    cache.set("d", ByteView("4"));

    ExpectEvicted(evicted, {{"a", "1"}});
    ExpectMissing(cache.get("a"));
    ExpectValue(cache.get("b"), "2");
    ExpectValue(cache.get("c"), "3");
    ExpectValue(cache.get("d"), "4");
}

TEST(LRUCacheTests, SetCanEvictMultipleEntries)
{
    EvictedEntries evicted;
    LRUCache cache(8, [&evicted](std::string key, ByteView value)
                   {
                       evicted.emplace_back(std::move(key), value.toString()); //
                   });

    cache.set("aa", ByteView("11"));
    cache.set("bb", ByteView("22"));
    cache.set("cc", ByteView("333333"));

    ExpectEvicted(evicted, {{"aa", "11"}, {"bb", "22"}});
    ExpectMissing(cache.get("aa"));
    ExpectMissing(cache.get("bb"));
    ExpectValue(cache.get("cc"), "333333");
}

TEST(LRUCacheTests, SetOversizedEntryEvictsItself)
{
    EvictedEntries evicted;
    LRUCache cache(10, [&evicted](std::string key, ByteView value)
                   {
                       evicted.emplace_back(std::move(key), value.toString()); //
                   });

    const std::string oversized(8, 'x');
    cache.set("key", ByteView(oversized));

    ExpectEvicted(evicted, {{"key", oversized}});
    ExpectMissing(cache.get("key"));
}

TEST(LRUCacheTests, ZeroMaxBytesDisablesCapacityEviction)
{
    EvictedEntries evicted;
    LRUCache cache(0, [&evicted](std::string key, ByteView value)
                   {
                       evicted.emplace_back(std::move(key), value.toString()); //
                   });

    for (int i = 0; i < 100; ++i)
    {
        const auto key = std::to_string(i);
        cache.set(key, ByteView(key));
    }

    for (int i = 0; i < 100; ++i)
    {
        const auto key = std::to_string(i);
        ExpectValue(cache.get(key), key);
    }

    EXPECT_TRUE(evicted.empty());
}

TEST(LRUCacheTests, DeleteByKeyRemovesValueAndNotifiesCaller)
{
    EvictedEntries evicted;
    LRUCache cache(0, [&evicted](std::string key, ByteView value)
                   {
                       evicted.emplace_back(std::move(key), value.toString()); //
                   });

    cache.set("a", ByteView("1"));
    cache.set("b", ByteView("2"));
    cache.set("c", ByteView("3"));
    cache.deleteByKey("b");

    ExpectEvicted(evicted, {{"b", "2"}});
    ExpectMissing(cache.get("b"));
    ExpectValue(cache.get("a"), "1");
    ExpectValue(cache.get("c"), "3");
}

TEST(LRUCacheTests, DeleteByKeyFreesCapacityBeforeNextSet)
{
    EvictedEntries evicted;
    LRUCache cache(6, [&evicted](std::string key, ByteView value)
                   {
                       evicted.emplace_back(std::move(key), value.toString()); //
                   });

    cache.set("a", ByteView("1"));
    cache.set("b", ByteView("2"));
    cache.set("c", ByteView("3"));
    cache.deleteByKey("b");
    cache.set("d", ByteView("4"));

    ExpectEvicted(evicted, {{"b", "2"}});
    ExpectValue(cache.get("a"), "1");
    ExpectValue(cache.get("c"), "3");
    ExpectValue(cache.get("d"), "4");
}

TEST(LRUCacheTests, DeleteMissingKeyDoesNotInvokeCallback)
{
    EvictedEntries evicted;
    LRUCache cache(0, [&evicted](std::string key, ByteView value)
                   {
                       evicted.emplace_back(std::move(key), value.toString()); //
                   });

    cache.set("a", ByteView("1"));
    cache.deleteByKey("missing");

    EXPECT_TRUE(evicted.empty());
    ExpectValue(cache.get("a"), "1");
}

TEST(LRUCacheTests, RemoveOldestEvictsLeastRecentlyUsedEntry)
{
    EvictedEntries evicted;
    LRUCache cache(0, [&evicted](std::string key, ByteView value)
                   {
                       evicted.emplace_back(std::move(key), value.toString()); //
                   });

    cache.set("a", ByteView("1"));
    cache.set("b", ByteView("2"));
    cache.set("c", ByteView("3"));
    ExpectValue(cache.get("a"), "1");
    cache.removeOldest();

    ExpectEvicted(evicted, {{"b", "2"}});
    ExpectValue(cache.get("a"), "1");
    ExpectValue(cache.get("c"), "3");
    ExpectMissing(cache.get("b"));
}

TEST(LRUCacheTests, RemoveOldestOnEmptyCacheDoesNothing)
{
    EvictedEntries evicted;
    LRUCache cache(0, [&evicted](std::string key, ByteView value)
                   {
                       evicted.emplace_back(std::move(key), value.toString()); //
                   });

    cache.removeOldest();

    EXPECT_TRUE(evicted.empty());
}

TEST(LRUCacheTests, EvictionCallbackPreservesBinaryValue)
{
    EvictedEntries evicted;
    LRUCache cache(3, [&evicted](std::string key, ByteView value)
                   {
                       evicted.emplace_back(std::move(key), value.toString()); //
                   });

    const std::string binary{std::string{'a', '\0', 'b'}};
    cache.set("k", ByteView(binary));

    ExpectEvicted(evicted, {{"k", binary}});
    ExpectMissing(cache.get("k"));
}

TEST(LRUCacheTests, ConcurrentSetAndGetAreSerialized)
{
    constexpr int threadCount = 8;
    constexpr int keysPerThread = 64;

    LRUCache cache(0);
    std::promise<void> start;
    auto startFuture = start.get_future();
    std::vector<std::thread> threads;
    threads.reserve(threadCount);

    for (int threadIndex = 0; threadIndex < threadCount; ++threadIndex)
    {
        threads.emplace_back([threadIndex, keysPerThread, &startFuture, &cache]()
                             {
                                 startFuture.wait();

                                 for (int keyIndex = 0; keyIndex < keysPerThread; ++keyIndex)
                                 {
                                     const auto key = std::to_string(threadIndex) + "-" + std::to_string(keyIndex);
                                     const auto value = "value-" + key;
                                     cache.set(key, ByteView(value));
                                     ExpectValue(cache.get(key), value);
                                 } //
                             });
    }

    start.set_value();

    for (auto &thread : threads) thread.join();

    for (int threadIndex = 0; threadIndex < threadCount; ++threadIndex)
    {
        for (int keyIndex = 0; keyIndex < keysPerThread; ++keyIndex)
        {
            const auto key = std::to_string(threadIndex) + "-" + std::to_string(keyIndex);
            ExpectValue(cache.get(key), "value-" + key);
        }
    }
}
