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
    if (node.empty()) return false;

    // 获取写锁。
    std::unique_lock lock(m_mtx);

    // 查询结点。
    auto iterNodeReplicas = m_nodeReplicas.find(node);
    if (iterNodeReplicas == m_nodeReplicas.end()) return false;

    int replicas = iterNodeReplicas->second;

    // 移除节点的所有虚拟节点。
    for (int i = 0; i < replicas; ++i)
    {
        std::string hashKey = fmt::format("{}-{}", node, std::to_string(i));
        uint32_t hash = m_config.m_hashFunc(hashKey);

        // 从哈希映射中移除。
        m_hashMap.erase(hash);

        // 从哈希环中移除哈希值。
        // std::remove 只移动元素并返回新的逻辑末尾，不会改变 vector 的大小；后续通过 erase 真正删除尾部区间。这样会移除 m_keys 中所有等于 hash 的值，因为重复值可能来自哈希碰撞，也可能来自重复添加节点。如果碰撞来自其他节点，这种按哈希值删除的方式可能误删其他虚拟节点。
        auto it = std::remove(m_keys.begin(), m_keys.end(), hash);
        m_keys.erase(it, m_keys.end());
    }

    m_nodeReplicas.erase(node);
    m_nodeCounts.erase(node);


    return true;
}

std::string ConsistentHashMap::get(const std::string &key)
{
    if (key.empty()) return "";

    // 获取读锁。
    std::shared_lock lock(m_mtx);

    if (m_keys.empty()) return "";

    uint32_t hash = m_config.m_hashFunc(key);
    // 二分查找：找到第一个大于等于 hash 的位置。
    auto it = std::lower_bound(m_keys.begin(), m_keys.end(), hash);
    // 处理边界情况（模拟环）：如果到了末尾，则回到开头。
    if (m_keys.end() == it) it = m_keys.begin();

    // 增加节点计数和总请求数，这里使用原子操作。
    std::string node = m_hashMap[*it];
    ++m_nodeCounts[node];
    ++m_totalRequests;


    return node;
}

std::unordered_map<std::string, double> ConsistentHashMap::getStats() const
{
    // 获取读锁。
    std::shared_lock lock(m_mtx);

    std::unordered_map<std::string, double> stats;
    long long currTotal = m_totalRequests.load();
    if (currTotal == 0) return stats;

    for (auto &[node, count] : m_nodeCounts) stats[node] = static_cast<double>(count.load()) / static_cast<double>(currTotal);


    return stats;
}

void ConsistentHashMap::addNode(const std::string &node, int replicas)
{
    for (int i = 0; i < replicas; ++i)
    {
        std::string hashKey = fmt::format("{}-{}", node, std::to_string(i));
        spdlog::debug("Adding virtual node: {} with hash key: {}", node, hashKey);

        uint32_t hash = m_config.m_hashFunc(hashKey);
        m_keys.push_back(hash);

        // 目前设计中，m_keys 只记录哈希值。如果不同虚拟节点产生相同哈希值，m_keys 会出现重复值；如果同一个节点被重复添加，也会产生重复值。m_hashMap 以哈希值为唯一键，发生碰撞时后写入的真实节点会覆盖先写入的节点。
        m_hashMap[hash] = node;
    }

    m_nodeReplicas[node] = replicas;

    // 如果节点是新添加的，初始化其计数器。
    if (0 == m_nodeCounts.count(node)) m_nodeCounts[node] = 0;
}

void ConsistentHashMap::checkAndRebalance()
{
    // 负载不均衡检测。
    // 1. 触发条件检查：
    // 当总请求数少于 1000 时，认为样本量不足，不进行负载均衡调整。
    // 通过读锁安全访问节点信息（m_nodeReplicas 和 m_nodeCounts）。
    // 2. 计算负载均衡度：
    // 计算平均负载：avgLoad = 总请求数 / 节点数。
    // 遍历所有节点，计算每个节点的负载与平均负载的差异百分比（diff / avgLoad）。
    // 记录最大差异百分比 maxDiff，作为负载不均衡度的指标。
    // 3. 触发重平衡：
    // 当 maxDiff 超过配置的阈值（m_config.m_loadBalanceThreshold）时，调用 RebalanceNodes() 进行重平衡。

    // 样本太少，不进行调整。
    if (m_totalRequests.load() < 1000) return;

    // 获取读锁。
    std::shared_lock lock(m_mtx);

    if (m_nodeReplicas.empty()) return;

    // 计算系统平均负载：总请求数 / 物理节点数量。
    long long currentTotalRequests = m_totalRequests.load();
    double avgLoad = static_cast<double>(currentTotalRequests) / m_nodeReplicas.size();
    double maxDiff = 0.0;

    // 遍历所有节点计算负载偏差，计算每个节点的负载与平均负载的差异百分比。
    for (auto &[node, count] : m_nodeCounts)
    {
        double diff = std::abs(static_cast<double>(count.load()) - avgLoad);
        // 避免除以零。
        if (avgLoad > 0)
        {
            if (diff / avgLoad > maxDiff) maxDiff = diff / avgLoad;
        }
        // 平均负载为 0 时，所有节点各自计数也应该都是 0。但由于统计数据暂时不一致，并发更新，计数器重置时机不同，节点增删过程中的中间状态等问题，可能导致当前节点仍有请求计数，此时统计状态不一致，按最大不均衡处理。
        else if (0 == avgLoad && diff > 0)
        {
            // 最大不均衡状态。
            maxDiff = 1.0;
        }
    }

    // 释放读锁，因为 rebalanceNodes 需要写锁。
    lock.unlock();

    // 如果负载不均衡度超过阈值，调整虚拟节点。
    if (maxDiff > m_config.m_loadBalanceThreshold) rebalanceNodes();
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
