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
{
}

ConsistentHashMap::~ConsistentHashMap()
{
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
}
