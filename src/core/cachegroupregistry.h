#ifndef _KCACHE_CACHE_GROUP_REGISTRY_H_
#define _KCACHE_CACHE_GROUP_REGISTRY_H_

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "cachegroup.h"


// 管理当前进程中的所有缓存组。每个缓存节点进程都有自己的注册表；该注册表不会跨进程共享数据。
class KCacheGroupRegistry
{

public:

    // 获取当前进程唯一的缓存组注册表。
    static KCacheGroupRegistry &Instance();

    KCacheGroupRegistry(const KCacheGroupRegistry &) = delete;

    KCacheGroupRegistry &operator=(const KCacheGroupRegistry &) = delete;

    KCacheGroupRegistry(KCacheGroupRegistry &&) = delete;

    KCacheGroupRegistry &operator=(KCacheGroupRegistry &&) = delete;

    // 注册缓存组。同名缓存组不会覆盖，重复注册会抛出异常。
    KCacheGroup &MakeCacheGroup(const std::string &name, int64_t bytes, DataGetter getter);

    // 获取已经注册的缓存组。当前注册表没有删除和替换操作，因此返回指针在进程结束前有效。
    KCacheGroup *GetCacheGroup(const std::string &name);


private:

    KCacheGroupRegistry() = default;

    ~KCacheGroupRegistry() = default;


    std::unordered_map<std::string, std::unique_ptr<KCacheGroup>> m_cacheGroups;

    std::mutex m_mtx;
};


#endif
