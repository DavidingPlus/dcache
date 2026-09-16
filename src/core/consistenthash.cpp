#include "consistenthash.h"

#include "crc32.h"

#include <mutex>
#include <algorithm>

#include <fmt/format.h>
#include <spdlog/spdlog.h>


namespace kcache
{

    // 默认配置：新节点初始拥有 10 个虚拟节点；动态调整时，数量限制在 [10, 200] 范围内；当最大相对负载偏差超过 25% 时触发重新平衡。
    // 项目当前使用 C++17。C++20 的指定初始化语法（例如 .member = value）在 MSVC 的 /std:c++17 模式下不可用，因此这里使用 C++17 兼容的方式：通过匿名 Lambda 创建一个临时配置对象，再逐个设置成员，最后末尾使用 () 立即调用。
    const HashConfig kDefaultHashConfig = []
    {
        HashConfig config{};

        config.m_defaultReplicas = 10;
        config.m_minReplicas = 10;
        config.m_maxReplicas = 200;
        config.m_hashFunc = crc32IEEE;
        config.m_loadBalanceThreshold = 0.25;


        return config;
    }();

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

        // 不允许重复添加结点。
        if (m_nodeReplicas.end() != m_nodeReplicas.find(node)) continue;

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

    // m_mtx 的读锁保护哈希环以及节点映射的容器结构；多个 get() 可以同时持有读锁。
    // m_nodeCounts 和 m_totalRequests 是原子变量，递增操作只修改原子变量的值，共享锁限制的是容器结构的修改，不限制原子变量值的原子更新，因此可以安全地在读锁中并发执行。这里不需要将读锁升级为写锁；只有增删容器元素等结构性修改才需要写锁。
    // 这里依赖 m_keys、m_hashMap 和 m_nodeCounts 始终保持同步，保证下标访问命中已有元素。
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
    if (0 == currTotal) return stats;

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
        // 平均负载为 0 时，所有节点各自计数也应该都是 0。但由于统计数据暂时不一致，并发更新，计数器重置时机不同，节点增删过程中的中间状态等问题，可能导致某个节点请求计数大于 0，此时统计状态不一致，按最大不均衡处理。
        else if (0 == avgLoad && diff > 0)
        {
            // 将 maxDiff 设为 1.0 的原因为：
            // 1. 表示最大不均衡状态。maxDiff 本质上是负载差异的相对比例。当 avgLoad = 0 但节点有请求时，说明这本身就是一种非常不平衡的状态。将 maxDiff 设为 1.0（即 100%），表示 "负载分配达到了最不均衡的状态"，需要强制触发重平衡。
            // 2. 确保重平衡被触发。在代码中，当 maxDiff > m_config.m_loadBalanceThreshold 时触发重平衡。若 maxDiff 不设为 1.0，可能导致 maxDiff 始终为 0，无法触发重平衡。
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
    // 节点再平衡。
    // 1. 获取写锁：
    // 由于需要修改哈希环结构，使用写锁确保线程安全。
    // 2. 计算负载比例：
    // 对每个节点，计算其负载比例 loadRatio = 节点请求数 / 平均负载。
    // 若 avgLoad 为 0 但节点有请求，将 loadRatio 设为 2.0（触发虚拟节点减少）。
    // 3. 调整虚拟节点数量：
    // 负载过高（loadRatio > 1.0）：减少虚拟节点数，公式为 旧虚拟节点数 / loadRatio。
    // 负载过低（loadRatio ≤ 1.0）：增加虚拟节点数，公式为 旧虚拟节点数 × (2.0 - loadRatio)。
    // 确保虚拟节点数在配置的 m_minReplicas 和 m_maxReplicas 范围内。
    // 4. 更新哈希环：
    // 移除旧的虚拟节点：从哈希环（hash_map_ 和 m_keys）中删除节点的所有虚拟节点。
    // 添加新的虚拟节点：调用 AddNode() 重新添加调整后的虚拟节点。
    // 5. 重置计数器：
    // 清空所有节点的请求计数和总请求数，为下一轮负载均衡做准备。
    // 重新排序哈希环：
    // 对 m_keys 排序，确保哈希环按哈希值有序排列，便于后续查找。

    // 获取写锁。
    std::unique_lock lock(m_mtx);

    if (m_nodeReplicas.empty()) return;

    long long currentTotalRequests = m_totalRequests.load();
    double avgLoad = static_cast<double>(currentTotalRequests) / m_nodeReplicas.size();

    // 调整每个节点的虚拟节点数量。
    // 注意：这里需要创建一个副本，因为在循环中可能会修改 m_nodeReplicas 和 m_nodeCounts。
    std::unordered_map<std::string, int> currReplicas = m_nodeReplicas;
    std::unordered_map<std::string, long long> currCounts;
    for (auto &[node, count] : m_nodeCounts) currCounts[node] = count.load();

    for (auto &[node, count] : currCounts)
    {
        int oldReplicas = currReplicas[node];

        double loadRatio = 0.0;
        // 正常计算负载比例。
        if (avgLoad > 0)
        {
            loadRatio = static_cast<double>(count) / avgLoad;
        }
        // avgLoad 为 0 但节点有请求，按照最高负载处理。
        else if (0 == avgLoad && count > 0)
        {
            loadRatio = 2.0;
        }
        // avgLoad 为 0 且节点无请求，无需调整。
        else
        {
            loadRatio = 1.0;
        }

        // 为什么负载过高就减少虚拟节点数?
        // 一致性哈希通过在哈希环上为每个物理节点创建多个虚拟节点，使请求更均匀地分布。虚拟节点数越多，节点在哈希环上的 "占位" 越多，分配到的请求也越多。
        // 而虚拟节点数减少后，该节点在哈希环上的 "覆盖范围" 变小，请求命中该节点的概率降低。且我们使用一致性哈希希望让请求均匀分布到各节点，若某个节点负载过高，说明其虚拟节点数过多，需要减少。

        // 负载过高（loadRatio > 1.0）：减少虚拟节点数，公式为 旧虚拟节点数 / loadRatio。
        // 负载过低（loadRatio ≤ 1.0）：增加虚拟节点数，公式为 旧虚拟节点数 × (2.0 - loadRatio)。
        int newReplicas = static_cast<int>(std::round(
            loadRatio > 1.0
                ? static_cast<double>(oldReplicas) / loadRatio
                : static_cast<double>(oldReplicas) * (2.0 - loadRatio)));

        // 确保在 newReplicas 限制范围内。
        if (newReplicas < m_config.m_minReplicas) newReplicas = m_config.m_minReplicas;
        if (newReplicas > m_config.m_maxReplicas) newReplicas = m_config.m_maxReplicas;

        // 虚拟节点重建。重新添加节点的虚拟节点：先移除旧的，再添加新的。
        if (newReplicas != oldReplicas)
        {
            // 移除节点的所有虚拟节点。
            int replicasToRemove = m_nodeReplicas[node];
            for (int i = 0; i < replicasToRemove; ++i)
            {
                std::string hashKey = node + "-" + std::to_string(i);
                uint32_t hash = static_cast<uint32_t>(m_config.m_hashFunc(hashKey));

                m_hashMap.erase(hash);

                auto it = std::remove(m_keys.begin(), m_keys.end(), hash);
                m_keys.erase(it, m_keys.end());
            }

            m_nodeReplicas.erase(node);

            // 添加新的虚拟节点。
            addNode(node, newReplicas);
        }
    }

    // 重置计数器。
    for (auto &pair : m_nodeCounts) pair.second.store(0);
    m_totalRequests.store(0);

    // 重新排序哈希环。
    std::sort(m_keys.begin(), m_keys.end());
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
