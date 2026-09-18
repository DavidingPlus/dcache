#ifndef _DCACHE_CACHE_GROUP_REGISTRY_H_
#define _DCACHE_CACHE_GROUP_REGISTRY_H_

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "globalmacros.h"
#include "cachegroup.h"


// 管理当前进程中的所有缓存组。每个缓存节点进程都有自己的注册表；该注册表不会跨进程共享数据。
class DCacheGroupRegistry
{

    CLASS_NONCOPYABLE(DCacheGroupRegistry)

public:

    // 获取当前进程唯一的缓存组注册表。
    static DCacheGroupRegistry &Instance();

    // 通常，我们不直接使用 DCacheGroup 的构造函数来创建缓存组，而是通过以下两个函数来操作（因为请求缓存节点时是通过 gRPC，那 gRPC Server 就应该接收请求后去创建/使用缓存组）。

    // 注册缓存组。同名缓存组不会覆盖，重复注册会抛出异常。
    DCacheGroup &MakeCacheGroup(const std::string &name, int64_t bytes, DataGetter getter);

    // 获取已经注册的缓存组。当前注册表没有删除和替换操作，因此返回指针在进程结束前有效。
    DCacheGroup *GetCacheGroup(const std::string &name);


private:

    DCacheGroupRegistry() = default;

    ~DCacheGroupRegistry() = default;


    std::unordered_map<std::string, std::unique_ptr<DCacheGroup>> m_cacheGroups;

    std::mutex m_mtx;
};


#endif
