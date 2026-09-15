#include "consistenthash.h"

#include "crc32.h"


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
