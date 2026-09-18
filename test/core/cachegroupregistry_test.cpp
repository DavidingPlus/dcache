#include <gtest/gtest.h>

#include "cachegroupregistry.h"

#include <atomic>
#include <future>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>


namespace
{

    std::string UniqueGroupName(const std::string &prefix)
    {
        static std::atomic<unsigned long long> nextId{0};
        return prefix + "-" + std::to_string(nextId.fetch_add(1, std::memory_order_relaxed));
    }


    DataGetter EmptyResultGetter()
    {
        return [](const std::string &) -> ByteViewOptional
        { return std::nullopt; };
    }

} // namespace


TEST(DCacheGroupRegistryTests, InstanceReturnsTheSameRegistry)
{
    auto &first = DCacheGroupRegistry::Instance();
    auto &second = DCacheGroupRegistry::Instance();

    EXPECT_EQ(&first, &second);
}

TEST(DCacheGroupRegistryTests, GetReturnsNullptrForMissingGroup)
{
    auto &registry = DCacheGroupRegistry::Instance();
    const auto missingGroupName = UniqueGroupName("registry-missing-group");

    EXPECT_EQ(nullptr, registry.GetCacheGroup(missingGroupName));
    EXPECT_EQ(nullptr, registry.GetCacheGroup(""));
}

TEST(DCacheGroupRegistryTests, MakeRegistersAndGetReturnsTheSameGroup)
{
    auto &registry = DCacheGroupRegistry::Instance();
    const auto groupName = UniqueGroupName("registry-create-and-get-group");

    auto &created = registry.MakeCacheGroup(
        groupName,
        0,
        EmptyResultGetter());

    EXPECT_EQ(&created, registry.GetCacheGroup(groupName));
}

TEST(DCacheGroupRegistryTests, RejectsEmptyName)
{
    auto &registry = DCacheGroupRegistry::Instance();

    EXPECT_THROW(
        registry.MakeCacheGroup("", 0, EmptyResultGetter()),
        std::invalid_argument);
}

TEST(DCacheGroupRegistryTests, RejectsEmptyGetter)
{
    auto &registry = DCacheGroupRegistry::Instance();
    const auto groupName = UniqueGroupName("registry-empty-getter-group");

    EXPECT_THROW(
        registry.MakeCacheGroup(groupName, 0, nullptr),
        std::invalid_argument);
    EXPECT_EQ(nullptr, registry.GetCacheGroup(groupName));
}


TEST(DCacheGroupRegistryTests, RegisteredGroupUsesGetterAndCachesValue)
{
    auto &registry = DCacheGroupRegistry::Instance();
    std::atomic<int> callCount{0};
    const auto groupName = UniqueGroupName("registry-usable-group");

    auto &group = registry.MakeCacheGroup(
        groupName,
        0,
        [&callCount](const std::string &key) -> ByteViewOptional
        {
            ++callCount;
            return ByteView("loaded:" + key);
        });

    const auto first = group.get("key");
    const auto second = group.get("key");

    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ("loaded:key", first->toString());
    EXPECT_EQ("loaded:key", second->toString());
    EXPECT_EQ(1, callCount.load());
}


TEST(DCacheGroupRegistryTests, DifferentGroupsKeepSameKeyValuesIndependent)
{
    auto &registry = DCacheGroupRegistry::Instance();
    const auto firstGroupName = UniqueGroupName("registry-isolated-group-a");
    const auto secondGroupName = UniqueGroupName("registry-isolated-group-b");

    auto &first = registry.MakeCacheGroup(
        firstGroupName,
        0,
        [](const std::string &) -> ByteViewOptional
        { return ByteView("group-a"); });
    auto &second = registry.MakeCacheGroup(
        secondGroupName,
        0,
        [](const std::string &) -> ByteViewOptional
        { return ByteView("group-b"); });

    const auto firstValue = first.get("same-key");
    const auto secondValue = second.get("same-key");

    ASSERT_TRUE(firstValue.has_value());
    ASSERT_TRUE(secondValue.has_value());
    EXPECT_EQ("group-a", firstValue->toString());
    EXPECT_EQ("group-b", secondValue->toString());
}

TEST(DCacheGroupRegistryTests, DuplicateNameDoesNotReplaceExistingGroup)
{
    auto &registry = DCacheGroupRegistry::Instance();
    const auto groupName = UniqueGroupName("registry-duplicate-group");

    auto &created = registry.MakeCacheGroup(
        groupName,
        0,
        EmptyResultGetter());

    EXPECT_THROW(
        registry.MakeCacheGroup(
            groupName,
            0,
            EmptyResultGetter()),
        std::invalid_argument);

    EXPECT_EQ(&created, registry.GetCacheGroup(groupName));
}

TEST(DCacheGroupRegistryTests, DifferentNamesCreateDifferentGroups)
{
    auto &registry = DCacheGroupRegistry::Instance();
    const auto firstGroupName = UniqueGroupName("registry-distinct-group-a");
    const auto secondGroupName = UniqueGroupName("registry-distinct-group-b");

    auto &first = registry.MakeCacheGroup(
        firstGroupName,
        0,
        EmptyResultGetter());
    auto &second = registry.MakeCacheGroup(
        secondGroupName,
        0,
        EmptyResultGetter());

    EXPECT_NE(&first, &second);
}

TEST(DCacheGroupRegistryTests, GroupAddressRemainsStableAfterMoreRegistrations)
{
    auto &registry = DCacheGroupRegistry::Instance();
    const auto groupName = UniqueGroupName("registry-stable-address-group");

    auto &created = registry.MakeCacheGroup(
        groupName,
        0,
        EmptyResultGetter());

    for (int i = 0; i < 32; ++i)
    {
        registry.MakeCacheGroup(
            groupName + "-" + std::to_string(i),
            0,
            EmptyResultGetter());
    }

    EXPECT_EQ(&created, registry.GetCacheGroup(groupName));
}

TEST(DCacheGroupRegistryTests, ConcurrentRegistrationsAreAllPreserved)
{
    auto &registry = DCacheGroupRegistry::Instance();
    constexpr int threadCount = 8;
    const auto groupPrefix = UniqueGroupName("registry-concurrent-group");

    std::promise<void> start;
    auto startFuture = start.get_future().share();
    std::vector<std::future<DCacheGroup *>> registrations;
    registrations.reserve(threadCount);

    for (int i = 0; i < threadCount; ++i)
    {
        registrations.emplace_back(std::async(
            std::launch::async,
            [&registry, startFuture, groupPrefix, i]() mutable -> DCacheGroup *
            {
                startFuture.wait();
                auto name = groupPrefix + "-" + std::to_string(i);
                return &registry.MakeCacheGroup(name, 0, EmptyResultGetter()); //
            }));
    }

    start.set_value();

    for (int i = 0; i < threadCount; ++i)
    {
        auto *created = registrations[i].get();
        auto name = groupPrefix + "-" + std::to_string(i);

        ASSERT_NE(nullptr, created);
        EXPECT_EQ(created, registry.GetCacheGroup(name));
    }
}

TEST(DCacheGroupRegistryTests, ConcurrentDuplicateRegistrationsOnlyAllowOne)
{
    auto &registry = DCacheGroupRegistry::Instance();
    constexpr int threadCount = 8;
    const auto groupName = UniqueGroupName("registry-concurrent-duplicate-group");

    std::promise<void> start;
    auto startFuture = start.get_future().share();
    std::vector<std::future<DCacheGroup *>> registrations;
    registrations.reserve(threadCount);

    for (int i = 0; i < threadCount; ++i)
    {
        registrations.emplace_back(std::async(
            std::launch::async,
            [&registry, startFuture, groupName]() mutable -> DCacheGroup *
            {
                startFuture.wait();

                try
                {
                    return &registry.MakeCacheGroup(
                        groupName,
                        0,
                        EmptyResultGetter());
                }
                catch (const std::invalid_argument &)
                {
                    return nullptr;
                } //
            }));
    }

    start.set_value();

    DCacheGroup *created = nullptr;
    int successfulRegistrations = 0;
    for (auto &registration : registrations)
    {
        auto *group = registration.get();
        if (group != nullptr)
        {
            ++successfulRegistrations;
            created = group;
        }
    }

    EXPECT_EQ(1, successfulRegistrations);
    ASSERT_NE(nullptr, created);
    EXPECT_EQ(created, registry.GetCacheGroup(groupName));
}

TEST(DCacheGroupRegistryTests, ConcurrentLookupsReturnTheRegisteredGroup)
{
    auto &registry = DCacheGroupRegistry::Instance();
    const auto groupName = UniqueGroupName("registry-concurrent-lookup-group");
    auto &created = registry.MakeCacheGroup(groupName, 0, EmptyResultGetter());

    constexpr int threadCount = 8;
    std::promise<void> start;
    auto startFuture = start.get_future().share();
    std::vector<std::future<DCacheGroup *>> lookups;
    lookups.reserve(threadCount);

    for (int i = 0; i < threadCount; ++i)
    {
        lookups.emplace_back(std::async(
            std::launch::async,
            [&registry, startFuture, groupName]() mutable -> DCacheGroup *
            {
                startFuture.wait();
                return registry.GetCacheGroup(groupName); //
            }));
    }

    start.set_value();

    for (auto &lookup : lookups) EXPECT_EQ(&created, lookup.get());
}
