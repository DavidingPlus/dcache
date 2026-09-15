#ifndef _KCACHE_CONSISTENT_HASH_H_
#define _KCACHE_CONSISTENT_HASH_H_

#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <shared_mutex>
#include <unordered_map>


// 一致性哈希配置。
struct HashConfig
{
    // 真实节点时默认分配的虚拟节点数量。这是初始值，不代表节点运行过程中的实际数量。
    int m_defaultReplicas;

    // 单个真实节点动态调整虚拟节点数量时允许的下限。
    int m_minReplicas;

    // 单个真实节点动态调整虚拟节点数量时允许的上限。
    int m_maxReplicas;

    // 将 key 或虚拟节点标识映射到哈希环位置的函数。所有使用同一哈希环的参与方必须使用相同的哈希函数。
    std::function<uint32_t(const std::string &)> m_hashFunc;

    // 最大相对负载偏差超过此值时，触发虚拟节点数量调整。例如 0.25 表示允许的最大偏差为 25%。
    double m_loadBalanceThreshold;
};


namespace kcache
{

    // 默认一致性哈希配置。
    // 这是命名空间级变量的声明，具体定义放在 consistenthash.cpp 中。使用 extern 让所有编译单元引用同一个配置对象；不要在头文件中使用 static 定义，否则每个包含该头文件的编译单元都会生成独立副本。
    // 1. 命名空间级变量：.h 用 extern，.cpp 定义；
    // 2. 类静态成员：.h 用 static 声明，.cpp 定义；
    // 3. 只在 .cpp 内部使用：直接在 .cpp 中 static 定义。
    extern const HashConfig kDefaultHashConfig;

} // namespace kcache


// Map 一致性哈希实现。
class ConsistentHashMap
{

public:

    // New 创建一致性哈希实例。
    explicit ConsistentHashMap(HashConfig cfg = kcache::kDefaultHashConfig);

    // 析构函数，确保负载均衡器线程正确停止。
    ~ConsistentHashMap();

    // add 添加节点。返回 true 表示成功，false 表示失败。
    bool add(const std::vector<std::string> &nodes);

    // remove 移除节点。返回 true 表示成功，false 表示失败。
    bool remove(const std::string &node);

    // get 获取节点。
    std::string get(const std::string &key);

    // getStats 获取负载统计信息。
    std::unordered_map<std::string, double> getStats();


private:

    // addNode 添加节点的虚拟节点。
    void addNode(const std::string &node, int replicas);

    // checkAndRebalance 检查并重新平衡虚拟节点。
    void checkAndRebalance();

    // rebalanceNodes 重新平衡节点。
    void rebalanceNodes();

    // startBalancer 启动负载均衡器线程。
    void startBalancer();


    // 读写互斥量。
    mutable std::shared_mutex m_mtx;

    // 配置信息。
    HashConfig m_config;

    // 哈希环。
    std::vector<uint32_t> m_keys;

    // 哈希环到节点的映射。
    std::unordered_map<uint32_t, std::string> m_hashMap;

    // 节点到虚拟节点数量的映射。
    std::unordered_map<std::string, int> m_nodeReplicas;

    // 节点负载统计。使用 std::atomic<long long> 保证对 m_nodeCounts 中每个节点计数的原子操作。
    std::unordered_map<std::string, std::atomic<long long>> m_nodeCounts;

    // 总请求数。
    std::atomic<long long> m_totalRequests;

    // 负载均衡器线程。
    std::thread m_balancerThread;

    // 控制负载均衡器线程停止的标志。
    std::atomic<bool> m_isBalancerStop;
};


#endif
