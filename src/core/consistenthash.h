#ifndef _KCACHE_CONSISTENT_HASH_H_
#define _KCACHE_CONSISTENT_HASH_H_

#include <cstdint>
#include <functional>
#include <string>


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


#endif
