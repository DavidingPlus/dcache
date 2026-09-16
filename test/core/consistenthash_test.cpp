#include <gtest/gtest.h>

#include "consistenthash.h"
#include "crc32.h"

#include <cstdint>
#include <string>
#include <thread>
#include <vector>


namespace
{

    // 构造一个只有一个虚拟节点的测试配置，并显式指定环上的位置。
    // node-a-0 位于 100，node-b-0 位于 300，因此：
    // - 哈希值 50 命中 node-a；
    // - 哈希值 150 命中 node-b；
    // - 哈希值 350 超过环尾，回绕后命中 node-a。
    HashConfig makeTestHashConfig()
    {
        HashConfig config = kcache::kDefaultHashConfig;
        config.m_defaultReplicas = 1;
        config.m_minReplicas = 1;
        config.m_maxReplicas = 1;

        config.m_hashFunc = [](const std::string &key) -> uint32_t
        {
            if (key == "node-a-0") return 100;
            if (key == "node-b-0") return 300;
            if (key == "key-before-first") return 50;
            if (key == "key-between-nodes") return 150;
            if (key == "key-after-last") return 350;

            // 未显式指定的 key 使用正常哈希函数，避免测试配置对其他 key 产生特殊语义。
            return kcache::crc32IEEE(key);
        };

        return config;
    }

    HashConfig makeHighHashValueConfig()
    {
        HashConfig config = makeTestHashConfig();

        config.m_hashFunc = [](const std::string &key) -> uint32_t
        {
            if (key == "node-low-0") return 100u;
            if (key == "node-high-0") return 0xF0000000u;
            if (key == "key-low") return 50u;
            if (key == "key-high") return 0xE0000000u;

            return kcache::crc32IEEE(key);
        };

        return config;
    }

} // namespace


TEST(HashConfigTests, ProvidesExpectedDefaultValues)
{
    const auto &config = kcache::kDefaultHashConfig;

    EXPECT_EQ(10, config.m_defaultReplicas);
    EXPECT_EQ(10, config.m_minReplicas);
    EXPECT_EQ(200, config.m_maxReplicas);
    EXPECT_DOUBLE_EQ(0.25, config.m_loadBalanceThreshold);
}

TEST(HashConfigTests, DefaultReplicaCountIsWithinConfiguredBounds)
{
    const auto &config = kcache::kDefaultHashConfig;

    EXPECT_LE(config.m_minReplicas, config.m_defaultReplicas);
    EXPECT_LE(config.m_defaultReplicas, config.m_maxReplicas);
}

TEST(HashConfigTests, UsesCrc32IEEEAsDefaultHashFunction)
{
    const auto &config = kcache::kDefaultHashConfig;
    const std::string key = "node-a-0";

    ASSERT_TRUE(config.m_hashFunc);
    EXPECT_EQ(kcache::crc32IEEE(key), config.m_hashFunc(key));
}

TEST(ConsistentHashMapTests, ReturnsEmptyWhenHashRingHasNoNodes)
{
    ConsistentHashMap hashMap(makeTestHashConfig());

    EXPECT_EQ("", hashMap.get("key-before-first"));
    EXPECT_TRUE(hashMap.getStats().empty());
}

TEST(ConsistentHashMapTests, RejectsEmptyNodeList)
{
    ConsistentHashMap hashMap(makeTestHashConfig());

    EXPECT_FALSE(hashMap.add({}));
    EXPECT_EQ("", hashMap.get("key-before-first"));
}

TEST(ConsistentHashMapTests, IgnoresEmptyNodesWhenAddingValidNodes)
{
    ConsistentHashMap hashMap(makeTestHashConfig());

    ASSERT_TRUE(hashMap.add({"", "node-a", ""}));

    EXPECT_EQ("node-a", hashMap.get("key-before-first"));
}

TEST(ConsistentHashMapTests, EmptyKeyDoesNotRouteOrUpdateStats)
{
    ConsistentHashMap hashMap(makeTestHashConfig());
    ASSERT_TRUE(hashMap.add({"node-a"}));

    EXPECT_EQ("", hashMap.get(""));
    EXPECT_TRUE(hashMap.getStats().empty());
}

TEST(ConsistentHashMapTests, RoutesKeysToTheFirstNodeAtOrAfterTheirHash)
{
    ConsistentHashMap hashMap(makeTestHashConfig());
    ASSERT_TRUE(hashMap.add({"node-a", "node-b"}));

    // 50 对应环上第一个位置 100，因此命中 node-a。
    EXPECT_EQ("node-a", hashMap.get("key-before-first"));

    // 150 大于 100 且小于 300，因此命中下一个虚拟节点 node-b。
    EXPECT_EQ("node-b", hashMap.get("key-between-nodes"));

    // 350 超过环尾，按照一致性哈希规则回绕到环首的 node-a。
    EXPECT_EQ("node-a", hashMap.get("key-after-last"));
}

TEST(ConsistentHashMapTests, RoutesTheSameKeyToTheSameNode)
{
    ConsistentHashMap hashMap(makeTestHashConfig());
    ASSERT_TRUE(hashMap.add({"node-a", "node-b"}));

    const std::string firstNode = hashMap.get("key-between-nodes");
    const std::string secondNode = hashMap.get("key-between-nodes");

    EXPECT_EQ("node-b", firstNode);
    EXPECT_EQ(firstNode, secondNode);
}

TEST(ConsistentHashMapTests, RemovingNodeRoutesKeysToTheRemainingNode)
{
    ConsistentHashMap hashMap(makeTestHashConfig());
    ASSERT_TRUE(hashMap.add({"node-a", "node-b"}));

    ASSERT_TRUE(hashMap.remove("node-a"));

    // 删除 node-a 后，环上只剩 node-b，所有 key 都应该路由到 node-b。
    EXPECT_EQ("node-b", hashMap.get("key-before-first"));
    EXPECT_EQ("node-b", hashMap.get("key-between-nodes"));
    EXPECT_EQ("node-b", hashMap.get("key-after-last"));

    // 同一个节点只能成功删除一次。
    EXPECT_FALSE(hashMap.remove("node-a"));
}

TEST(ConsistentHashMapTests, RemovingAllNodesMakesTheHashRingEmpty)
{
    ConsistentHashMap hashMap(makeTestHashConfig());
    ASSERT_TRUE(hashMap.add({"node-a"}));

    ASSERT_TRUE(hashMap.remove("node-a"));

    EXPECT_EQ("", hashMap.get("key-before-first"));
    EXPECT_TRUE(hashMap.getStats().empty());
}

TEST(ConsistentHashMapTests, RejectsRemovingEmptyOrUnknownNodes)
{
    ConsistentHashMap hashMap(makeTestHashConfig());

    EXPECT_FALSE(hashMap.remove(""));
    EXPECT_FALSE(hashMap.remove("unknown-node"));

    ASSERT_TRUE(hashMap.add({"node-a"}));
    EXPECT_FALSE(hashMap.remove("unknown-node"));
}

TEST(ConsistentHashMapTests, CanAddNodeAgainAfterRemovingIt)
{
    ConsistentHashMap hashMap(makeTestHashConfig());

    ASSERT_TRUE(hashMap.add({"node-a"}));
    ASSERT_TRUE(hashMap.remove("node-a"));
    ASSERT_TRUE(hashMap.add({"node-a"}));

    EXPECT_EQ("node-a", hashMap.get("key-before-first"));
}

TEST(ConsistentHashMapTests, ReportsRequestDistributionByNode)
{
    ConsistentHashMap hashMap(makeTestHashConfig());
    ASSERT_TRUE(hashMap.add({"node-a", "node-b"}));

    // 添加节点后尚未路由请求，不应该产生统计数据。
    EXPECT_TRUE(hashMap.getStats().empty());

    // node-a 承接两次请求，node-b 承接一次请求。
    EXPECT_EQ("node-a", hashMap.get("key-before-first"));
    EXPECT_EQ("node-a", hashMap.get("key-after-last"));
    EXPECT_EQ("node-b", hashMap.get("key-between-nodes"));

    const auto stats = hashMap.getStats();

    ASSERT_EQ(2u, stats.size());
    EXPECT_NEAR(2.0 / 3.0, stats.at("node-a"), 1e-9);
    EXPECT_NEAR(1.0 / 3.0, stats.at("node-b"), 1e-9);
}

TEST(ConsistentHashMapTests, RemovesDeletedNodeFromStats)
{
    ConsistentHashMap hashMap(makeTestHashConfig());
    ASSERT_TRUE(hashMap.add({"node-a", "node-b"}));

    EXPECT_EQ("node-a", hashMap.get("key-before-first"));
    EXPECT_EQ("node-b", hashMap.get("key-between-nodes"));

    ASSERT_TRUE(hashMap.remove("node-a"));

    const auto stats = hashMap.getStats();

    ASSERT_EQ(1u, stats.size());
    EXPECT_EQ(stats.end(), stats.find("node-a"));
    EXPECT_NE(stats.end(), stats.find("node-b"));
}

TEST(ConsistentHashMapTests, SupportsHashValuesAboveSignedIntRange)
{
    ConsistentHashMap hashMap(makeHighHashValueConfig());
    ASSERT_TRUE(hashMap.add({"node-low", "node-high"}));

    EXPECT_EQ("node-low", hashMap.get("key-low"));
    EXPECT_EQ("node-high", hashMap.get("key-high"));

    ASSERT_TRUE(hashMap.remove("node-high"));
    EXPECT_EQ("node-low", hashMap.get("key-high"));
}

TEST(ConsistentHashMapTests, SupportsConcurrentAtomicRequestCounting)
{
    ConsistentHashMap hashMap(makeTestHashConfig());
    ASSERT_TRUE(hashMap.add({"node-a", "node-b"}));

    constexpr int threadCount = 4;
    constexpr int requestsPerKeyPerThread = 100;
    std::vector<std::thread> workers;
    workers.reserve(threadCount);

    for (int i = 0; i < threadCount; ++i)
    {
        workers.emplace_back(
            [&hashMap, requestsPerKeyPerThread]()
            {
                for (int j = 0; j < requestsPerKeyPerThread; ++j)
                {
                    hashMap.get("key-before-first");
                    hashMap.get("key-between-nodes");
                }
            });
    }

    for (auto &worker : workers) worker.join();

    const auto stats = hashMap.getStats();

    ASSERT_EQ(2u, stats.size());
    EXPECT_NEAR(0.5, stats.at("node-a"), 1e-9);
    EXPECT_NEAR(0.5, stats.at("node-b"), 1e-9);
}
