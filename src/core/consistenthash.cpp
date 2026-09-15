#include "consistenthash.h"

#include "crc32.h"

#include <mutex>
#include <algorithm>

#include <fmt/format.h>
#include <spdlog/spdlog.h>


namespace kcache
{

    // 默认配置：新节点初始拥有 10 个虚拟节点；动态调整时，数量限制在 [10, 200] 范围内；当最大相对负载偏差超过 25% 时触发重新平衡。
    const HashConfig kDefaultHashConfig = {
        .m_defaultReplicas = 10,
        .m_minReplicas = 10,
        .m_maxReplicas = 200,
        .m_hashFunc = crc32IEEE,
        .m_loadBalanceThreshold = 0.25,
    };

} // namespace kcache


ConsistentHashMap::ConsistentHashMap(HashConfig cfg)
    : m_config(cfg), m_totalRequests(0), m_isBalancerStop(false)
{
    // 启动负载均衡器。
    startBalancer();
}

ConsistentHashMap::~ConsistentHashMap()
{
    m_isBalancerStop = true;
    // 等待负载均衡器线程完成。
    if (m_balancerThread.joinable()) m_balancerThread.join();
}

bool ConsistentHashMap::add(const std::vector<std::string> &nodes)
{
    if (nodes.empty()) return false;

    // 获取写锁。
    std::unique_lock lock(m_mtx);

    for (auto &node : nodes)
    {
        if (node.empty()) continue;

        // 为每个真实节点添加虚拟节点。
        addNode(node, m_config.m_defaultReplicas);
    }

    // 重新排序哈希环。
    std::sort(m_keys.begin(), m_keys.end());


    return true;
}

bool ConsistentHashMap::remove(const std::string &node)
{
}

std::string ConsistentHashMap::get(const std::string &key)
{
}

std::unordered_map<std::string, double> ConsistentHashMap::getStats()
{
}

void ConsistentHashMap::addNode(const std::string &node, int replicas)
{
    for (int i = 0; i < replicas; ++i)
    {
        std::string hashKey = fmt::format("{}-{}", node, std::to_string(i));
        spdlog::debug("Adding virtual node: {} with hash key: {}", node, hashKey);

        uint32_t hash = m_config.m_hashFunc(hashKey);
        m_keys.push_back(hash);
        m_hashMap[hash] = node;
    }

    m_nodeReplicas[node] = replicas;

    // 如果节点是新添加的，初始化其计数器。
    if (0 == m_nodeCounts.count(node)) m_nodeCounts[node] = 0;
}

void ConsistentHashMap::checkAndRebalance()
{
}

void ConsistentHashMap::rebalanceNodes()
{
}

void ConsistentHashMap::startBalancer()
{
    m_isBalancerStop = false;

    m_balancerThread = std::thread(
        [this]()
        {
            while (!m_isBalancerStop)
            {
                // 每秒检查一次。
                std::this_thread::sleep_for(std::chrono::seconds(1));

                // 再次检查，防止在 sleep 期间被要求停止。
                if (!m_isBalancerStop) checkAndRebalance();
            } //
        });
}
