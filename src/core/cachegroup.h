#ifndef _Km_cachem_cacheGROUP_H_
#define _Km_cachem_cacheGROUP_H_

#include <functional>
#include <string>
#include <atomic>
#include <memory>

#include "lrucache.h"
#include "singleflight.h"


struct GroupStatus
{
    std::atomic_int64_t m_loads;        // 加载次数。
    std::atomic_int64_t m_localHits;    // 本地缓存命中次数。
    std::atomic_int64_t m_localMisses;  // 本地缓存未命中次数。
    std::atomic_int64_t m_peerHits;     // 从对等节点获取成功次数。
    std::atomic_int64_t m_peerMisses;   // 从对等节点获取失败次数。
    std::atomic_int64_t m_loaderHits;   // 从加载器获取成功次数。
    std::atomic_int64_t m_loaderErrors; // 从加载器获取失败次数。
    std::atomic_int64_t m_loadDuration; // 加载总耗时（纳秒）。
};

enum class SyncFlag
{
    SET,
    DELETE,
    INVALIDATE, // 缓存失效，只删除本地缓存，不通过 getter 重新加载。
};


class KCacheGroup
{

    using DataGetter = std::function<ByteViewOptional(const std::string &key)>;

public:

    KCacheGroup() = default;

    KCacheGroup(std::string name, int64_t bytes, DataGetter getter) : m_cache(std::make_unique<LRUCache>(bytes)), m_name(name), m_getter(getter) {}

    KCacheGroup(const KCacheGroup &) = delete;

    KCacheGroup &operator=(const KCacheGroup &other) = delete;

    KCacheGroup(KCacheGroup &&other) : m_cache(std::move(other.m_cache)), m_name(std::move(other.m_name)), m_getter(std::move(other.m_getter)) {}

    KCacheGroup &operator=(KCacheGroup &&other);

    ByteViewOptional get(const std::string &key);

    bool set(const std::string &key, ByteView b);

    bool deleteByKey(const std::string &key);

    // 处理来自其他节点的失效请求。
    bool invalidateFromPeer(const std::string &key);


    static KCacheGroup &MakeCacheGroup(const std::string &name, int64_t bytes, DataGetter getter);

    static KCacheGroup *GetCacheGroup(const std::string &name);


private:

    ByteViewOptional load(const std::string &key);

    ByteViewOptional loadData(const std::string &key);


    std::unique_ptr<LRUCache> m_cache;

    std::string m_name;

    std::atomic<bool> m_isClose{false};

    DataGetter m_getter;

    SingleFlight m_loader;

    GroupStatus m_status;
};


#endif
