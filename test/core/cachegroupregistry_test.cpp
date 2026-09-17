#include <gtest/gtest.h>

#include "cachegroupregistry.h"

#include <future>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>


namespace
{

    DataGetter EmptyResultGetter()
    {
        return [](const std::string &) -> ByteViewOptional
        { return std::nullopt; };
    }

} // namespace


TEST(KCacheGroupRegistryTests, InstanceReturnsTheSameRegistry)
{
    auto &first = KCacheGroupRegistry::Instance();
    auto &second = KCacheGroupRegistry::Instance();

    EXPECT_EQ(&first, &second);
}

TEST(KCacheGroupRegistryTests, GetReturnsNullptrForMissingGroup)
{
    auto &registry = KCacheGroupRegistry::Instance();

    EXPECT_EQ(nullptr, registry.GetCacheGroup("registry-missing-group"));
    EXPECT_EQ(nullptr, registry.GetCacheGroup(""));
}

TEST(KCacheGroupRegistryTests, MakeRegistersAndGetReturnsTheSameGroup)
{
    auto &registry = KCacheGroupRegistry::Instance();

    auto &created = registry.MakeCacheGroup(
        "registry-create-and-get-group",
        0,
        EmptyResultGetter());

    EXPECT_EQ(&created, registry.GetCacheGroup("registry-create-and-get-group"));
}

TEST(KCacheGroupRegistryTests, RejectsEmptyName)
{
    auto &registry = KCacheGroupRegistry::Instance();

    EXPECT_THROW(
        registry.MakeCacheGroup("", 0, EmptyResultGetter()),
        std::invalid_argument);
}

TEST(KCacheGroupRegistryTests, RejectsEmptyGetter)
{
    auto &registry = KCacheGroupRegistry::Instance();

    EXPECT_THROW(
        registry.MakeCacheGroup("registry-empty-getter-group", 0, nullptr),
        std::invalid_argument);
}

TEST(KCacheGroupRegistryTests, DuplicateNameDoesNotReplaceExistingGroup)
{
    auto &registry = KCacheGroupRegistry::Instance();

    auto &created = registry.MakeCacheGroup(
        "registry-duplicate-group",
        0,
        EmptyResultGetter());

    EXPECT_THROW(
        registry.MakeCacheGroup(
            "registry-duplicate-group",
            0,
            EmptyResultGetter()),
        std::invalid_argument);

    EXPECT_EQ(&created, registry.GetCacheGroup("registry-duplicate-group"));
}

TEST(KCacheGroupRegistryTests, DifferentNamesCreateDifferentGroups)
{
    auto &registry = KCacheGroupRegistry::Instance();

    auto &first = registry.MakeCacheGroup(
        "registry-distinct-group-a",
        0,
        EmptyResultGetter());
    auto &second = registry.MakeCacheGroup(
        "registry-distinct-group-b",
        0,
        EmptyResultGetter());

    EXPECT_NE(&first, &second);
}

TEST(KCacheGroupRegistryTests, GroupAddressRemainsStableAfterMoreRegistrations)
{
    auto &registry = KCacheGroupRegistry::Instance();

    auto &created = registry.MakeCacheGroup(
        "registry-stable-address-group",
        0,
        EmptyResultGetter());

    for (int i = 0; i < 32; ++i)
    {
        registry.MakeCacheGroup(
            "registry-stable-address-group-" + std::to_string(i),
            0,
            EmptyResultGetter());
    }

    EXPECT_EQ(&created, registry.GetCacheGroup("registry-stable-address-group"));
}

TEST(KCacheGroupRegistryTests, ConcurrentRegistrationsAreAllPreserved)
{
    auto &registry = KCacheGroupRegistry::Instance();
    constexpr int threadCount = 8;

    std::promise<void> start;
    auto startFuture = start.get_future().share();
    std::vector<std::future<KCacheGroup *>> registrations;
    registrations.reserve(threadCount);

    for (int i = 0; i < threadCount; ++i)
    {
        registrations.emplace_back(std::async(
            std::launch::async,
            [&registry, startFuture, i]() mutable -> KCacheGroup *
            {
                startFuture.wait();
                auto name = "registry-concurrent-group-" + std::to_string(i);
                return &registry.MakeCacheGroup(name, 0, EmptyResultGetter()); //
            }));
    }

    start.set_value();

    for (int i = 0; i < threadCount; ++i)
    {
        auto *created = registrations[i].get();
        auto name = "registry-concurrent-group-" + std::to_string(i);

        ASSERT_NE(nullptr, created);
        EXPECT_EQ(created, registry.GetCacheGroup(name));
    }
}

TEST(KCacheGroupRegistryTests, ConcurrentDuplicateRegistrationsOnlyAllowOne)
{
    auto &registry = KCacheGroupRegistry::Instance();
    constexpr int threadCount = 8;
    constexpr auto groupName = "registry-concurrent-duplicate-group";

    std::promise<void> start;
    auto startFuture = start.get_future().share();
    std::vector<std::future<KCacheGroup *>> registrations;
    registrations.reserve(threadCount);

    for (int i = 0; i < threadCount; ++i)
    {
        registrations.emplace_back(std::async(
            std::launch::async,
            [&registry, startFuture, groupName]() mutable -> KCacheGroup *
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

    KCacheGroup *created = nullptr;
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

TEST(KCacheGroupRegistryTests, ConcurrentLookupsReturnTheRegisteredGroup)
{
    auto &registry = KCacheGroupRegistry::Instance();
    constexpr auto groupName = "registry-concurrent-lookup-group";
    auto &created = registry.MakeCacheGroup(groupName, 0, EmptyResultGetter());

    constexpr int threadCount = 8;
    std::promise<void> start;
    auto startFuture = start.get_future().share();
    std::vector<std::future<KCacheGroup *>> lookups;
    lookups.reserve(threadCount);

    for (int i = 0; i < threadCount; ++i)
    {
        lookups.emplace_back(std::async(
            std::launch::async,
            [&registry, startFuture, groupName]() mutable -> KCacheGroup *
            {
                startFuture.wait();
                return registry.GetCacheGroup(groupName); //
            }));
    }

    start.set_value();

    for (auto &lookup : lookups) EXPECT_EQ(&created, lookup.get());
}
