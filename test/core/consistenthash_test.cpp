#include <gtest/gtest.h>

#include "consistenthash.h"
#include "crc32.h"

#include <cstdint>
#include <string>
#include <thread>
#include <vector>
#include <unordered_set>


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


/*
    摘抄自 https://github.com/youngyangyang04/KamaCache-CPP/blob/main/test/test_consistent_hash.cpp
*/

class ConsistentHashTest : public ::testing::Test
{

protected:

    void SetUp() override
    {
        // Simple hash function for predictable testing
        m_testConfig = HashConfig{
            3,  // replicas
            1,  // min_replicas
            10, // max_replicas
            std::hash<std::string>{},
            0.2 // load_balance_threshold
        };
    }


    HashConfig m_testConfig;
};

TEST_F(ConsistentHashTest, DefaultConstructor)
{
    ConsistentHashMap hashMap;

    // Should work with default configuration
    EXPECT_TRUE(hashMap.add({"node1", "node2"}));

    auto node = hashMap.get("test_key");
    EXPECT_TRUE(node == "node1" || node == "node2");
}

TEST_F(ConsistentHashTest, CustomConfigConstructor)
{
    ConsistentHashMap hashMap(m_testConfig);

    EXPECT_TRUE(hashMap.add({"node1"}));
    auto node = hashMap.get("test_key");
    EXPECT_EQ(node, "node1");
}

TEST_F(ConsistentHashTest, BasicAddAndGet)
{
    ConsistentHashMap hashMap(m_testConfig);

    // Add nodes
    EXPECT_TRUE(hashMap.add({"node1", "node2", "node3"}));

    // Test that Get returns one of the added nodes
    std::unordered_set<std::string> expectedNodes = {"node1", "node2", "node3"};

    for (int i = 0; i < 100; ++i)
    {
        std::string key = "key" + std::to_string(i);
        auto node = hashMap.get(key);
        EXPECT_TRUE(expectedNodes.count(node) > 0);
    }
}

TEST_F(ConsistentHashTest, ConsistentHashing)
{
    ConsistentHashMap hashMap(m_testConfig);

    // Add initial nodes
    EXPECT_TRUE(hashMap.add({"node1", "node2"}));

    // Record which node each key maps to
    std::unordered_map<std::string, std::string> keyToNode;
    std::vector<std::string> testKeys;

    for (int i = 0; i < 50; ++i)
    {
        std::string key = "key" + std::to_string(i);
        testKeys.push_back(key);
        keyToNode[key] = hashMap.get(key);
    }

    // Add another node
    EXPECT_TRUE(hashMap.add({"node3"}));

    // Check that most keys still map to the same nodes
    int unchangedKeys = 0;
    for (const auto &key : testKeys)
    {
        if (hashMap.get(key) == keyToNode[key])
        {
            unchangedKeys++;
        }
    }

    // With consistent hashing, most keys should remain unchanged
    EXPECT_GT(unchangedKeys, testKeys.size() * 0.6); // At least 60% should be unchanged
}

TEST_F(ConsistentHashTest, RemoveNode)
{
    ConsistentHashMap hashMap(m_testConfig);

    // Add nodes
    EXPECT_TRUE(hashMap.add({"node1", "node2", "node3"}));

    // Remove a node
    EXPECT_TRUE(hashMap.remove("node2"));

    // Verify node2 is no longer returned
    std::unordered_set<std::string> possibleNodes;
    for (int i = 0; i < 100; ++i)
    {
        std::string key = "key" + std::to_string(i);
        auto node = hashMap.get(key);
        possibleNodes.insert(node);
    }

    EXPECT_EQ(possibleNodes.count("node2"), 0);
    EXPECT_GT(possibleNodes.count("node1"), 0);
    EXPECT_GT(possibleNodes.count("node3"), 0);
}

TEST_F(ConsistentHashTest, RemoveNonExistentNode)
{
    ConsistentHashMap hashMap(m_testConfig);

    EXPECT_TRUE(hashMap.add({"node1"}));

    // Removing non-existent node should handle gracefully
    bool result = hashMap.remove("nonexistent");
    // The behavior may vary based on implementation
    // Just ensure it doesn't crash

    // Original node should still work
    auto node = hashMap.get("test_key");
    EXPECT_EQ(node, "node1");
}

TEST_F(ConsistentHashTest, EmptyHashMap)
{
    ConsistentHashMap hashMap(m_testConfig);

    // Getting from empty hash map should return empty string or handle gracefully
    auto node = hashMap.get("test_key");
    // Implementation may return empty string or throw
    // Just ensure it doesn't crash
}

TEST_F(ConsistentHashTest, LoadBalanceDistribution)
{
    ConsistentHashMap hashMap(m_testConfig);

    // Add nodes
    EXPECT_TRUE(hashMap.add({"node1", "node2", "node3"}));

    // Generate many requests to test load distribution
    std::unordered_map<std::string, int> nodeCounts;
    int totalRequests = 10000;

    for (int i = 0; i < totalRequests; ++i)
    {
        std::string key = "key" + std::to_string(i);
        auto node = hashMap.get(key);
        nodeCounts[node]++;
    }

    // Check that load is reasonably distributed
    EXPECT_EQ(nodeCounts.size(), 3);

    for (const auto &[node, count] : nodeCounts)
    {
        double loadRatio = static_cast<double>(count) / totalRequests;
        // Each node should get roughly 1/3 of the load, allow some variance
        EXPECT_GT(loadRatio, 0.2);
        EXPECT_LT(loadRatio, 0.5);
    }
}

TEST_F(ConsistentHashTest, GetStats)
{
    ConsistentHashMap hashMap(m_testConfig);

    EXPECT_TRUE(hashMap.add({"node1", "node2"}));

    // Make some requests
    for (int i = 0; i < 100; ++i)
    {
        hashMap.get("key" + std::to_string(i));
    }

    // Get statistics
    auto stats = hashMap.getStats();

    // Should have stats for both nodes
    EXPECT_TRUE(stats.count("node1") > 0 || stats.count("node2") > 0);

    // Stats should be reasonable (between 0 and 1)
    for (const auto &[node, ratio] : stats)
    {
        EXPECT_GE(ratio, 0.0);
        EXPECT_LE(ratio, 1.0);
    }
}

TEST_F(ConsistentHashTest, ThreadSafety)
{
    ConsistentHashMap hashMap(m_testConfig);

    EXPECT_TRUE(hashMap.add({"node1", "node2", "node3"}));

    std::atomic<int> successfulGets{0};
    std::atomic<bool> stopFlag{false};

    // Start multiple reader threads
    std::vector<std::thread> readers;
    for (int i = 0; i < 4; ++i)
    {
        readers.emplace_back([&hashMap, &successfulGets, &stopFlag, i]()
                             {
            while (!stopFlag.load()) {
                std::string key = "thread" + std::to_string(i) + "_key" + std::to_string(successfulGets.load());
                auto node = hashMap.get(key);
                if (!node.empty()) {
                    successfulGets++;
                }
                std::this_thread::sleep_for(std::chrono::microseconds(10));
            } });
    }

    // Start a writer thread
    std::thread writer([&hashMap, &stopFlag]()
                       {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        hashMap.add({"node4"});
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        hashMap.remove("node4");
        stopFlag.store(true); });

    writer.join();
    for (auto &reader : readers)
    {
        reader.join();
    }

    EXPECT_GT(successfulGets.load(), 0);
}

TEST_F(ConsistentHashTest, MultipleAddOperations)
{
    ConsistentHashMap hashMap(m_testConfig);

    // Add nodes in multiple batches
    EXPECT_TRUE(hashMap.add({"node1"}));
    EXPECT_TRUE(hashMap.add({"node2", "node3"}));
    EXPECT_TRUE(hashMap.add({"node4"}));

    // Verify all nodes are accessible
    std::unordered_set<std::string> foundNodes;
    for (int i = 0; i < 1000; ++i)
    {
        std::string key = "key" + std::to_string(i);
        auto node = hashMap.get(key);
        foundNodes.insert(node);
    }

    EXPECT_EQ(foundNodes.size(), 4);
    EXPECT_TRUE(foundNodes.count("node1") > 0);
    EXPECT_TRUE(foundNodes.count("node2") > 0);
    EXPECT_TRUE(foundNodes.count("node3") > 0);
    EXPECT_TRUE(foundNodes.count("node4") > 0);
}

TEST_F(ConsistentHashTest, DuplicateNodeAddition)
{
    ConsistentHashMap hashMap(m_testConfig);

    EXPECT_TRUE(hashMap.add({"node1", "node2"}));

    // Try to add duplicate nodes
    bool result = hashMap.add({"node1", "node3"});
    // Implementation may handle duplicates differently
    // Just ensure it doesn't crash

    auto node = hashMap.get("test_key");
    EXPECT_FALSE(node.empty());
}

TEST_F(ConsistentHashTest, SpecificHashBehavior)
{
    // Test with a simple, predictable hash function
    HashConfig simpleConfig = m_testConfig;
    simpleConfig.m_defaultReplicas = 1; // Use fewer replicas for predictable testing
    simpleConfig.m_hashFunc = [](const std::string &key) -> uint32_t
    {
        if (key == "2") return 2;
        if (key == "4") return 4;
        if (key == "6") return 6;
        if (key == "8") return 8;
        if (key == "11") return 11;
        if (key == "23") return 23;
        if (key == "27") return 27;
        // For node names, create virtual nodes
        if (key == "2_0") return 2;
        if (key == "4_0") return 4;
        if (key == "6_0") return 6;
        if (key == "8_0") return 8;
        return std::hash<std::string>{}(key);
    };

    ConsistentHashMap hashMap(simpleConfig);

    // This test is based on the example, but may need adjustment
    // depending on the exact virtual node generation algorithm
    EXPECT_TRUE(hashMap.add({"6", "4", "2"}));

    // Test some key mappings
    auto node = hashMap.get("2");
    EXPECT_FALSE(node.empty());

    node = hashMap.get("11");
    EXPECT_FALSE(node.empty());
}

TEST_F(ConsistentHashTest, ConfigValidation)
{
    // Test with extreme configurations
    HashConfig extremeConfig = m_testConfig;
    extremeConfig.m_defaultReplicas = 1000; // Very high replicas
    extremeConfig.m_minReplicas = 500;
    extremeConfig.m_maxReplicas = 2000;

    ConsistentHashMap hashMap(extremeConfig);
    EXPECT_TRUE(hashMap.add({"node1"}));

    auto node = hashMap.get("test_key");
    EXPECT_EQ(node, "node1");
}

TEST_F(ConsistentHashTest, LongRunningBalancer)
{
    ConsistentHashMap hashMap(m_testConfig);

    EXPECT_TRUE(hashMap.add({"node1", "node2"}));

    // Make many requests to trigger balancer activity
    for (int i = 0; i < 1000; ++i)
    {
        hashMap.get("key" + std::to_string(i));
        if (i % 100 == 0)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    // Add more nodes to trigger rebalancing
    EXPECT_TRUE(hashMap.add({"node3", "node4"}));

    // Continue making requests
    for (int i = 1000; i < 2000; ++i)
    {
        hashMap.get("key" + std::to_string(i));
    }

    auto stats = hashMap.getStats();
    EXPECT_GE(stats.size(), 2);
}

// Test destructor behavior - ensure balancer thread stops properly
TEST_F(ConsistentHashTest, DestructorTest)
{
    {
        ConsistentHashMap hashMap(m_testConfig);
        EXPECT_TRUE(hashMap.add({"node1", "node2"}));

        // Make some requests
        for (int i = 0; i < 100; ++i)
        {
            hashMap.get("key" + std::to_string(i));
        }

        // hashMap will be destroyed here
    }

    // If we reach here without hanging, destructor worked correctly
    SUCCEED();
}
