#include <gtest/gtest.h>

#include "consistenthash.h"
#include "crc32.h"

#include <string>


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
