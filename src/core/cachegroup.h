#ifndef _KCACHE_CACHE_GROUP_H_
#define _KCACHE_CACHE_GROUP_H_

#include <functional>
#include <string>
#include <atomic>
#include <memory>

#include "lrucache.h"
#include "singleflight.h"


struct GroupStatus
{
    std::atomic_int64_t m_loads{0};        // 加载次数。
    std::atomic_int64_t m_localHits{0};    // 本地缓存命中次数。
    std::atomic_int64_t m_localMisses{0};  // 本地缓存未命中次数。
    std::atomic_int64_t m_peerHits{0};     // 从对等节点获取成功次数。
    std::atomic_int64_t m_peerMisses{0};   // 从对等节点获取失败次数。
    std::atomic_int64_t m_loaderHits{0};   // 从加载器获取成功次数。
    std::atomic_int64_t m_loaderErrors{0}; // 从加载器获取失败次数。
    std::atomic_int64_t m_loadDuration{0}; // 加载总耗时（纳秒）。
};

enum class SyncFlag
{
    SET,
    DELETE,
    INVALIDATE, // 缓存失效，只删除本地缓存，不通过 getter 重新加载。
};

using DataGetter = std::function<ByteViewOptional(const std::string &key)>;


class KCacheGroup
{

public:

    KCacheGroup() = default;

    KCacheGroup(std::string name, int64_t bytes, DataGetter getter) : m_cache(std::make_unique<LRUCache>(bytes)), m_name(name), m_getter(getter) {}

    KCacheGroup(const KCacheGroup &) = delete;

    KCacheGroup &operator=(const KCacheGroup &other) = delete;

    KCacheGroup(KCacheGroup &&other) : m_cache(std::move(other.m_cache)), m_name(std::move(other.m_name)), m_getter(std::move(other.m_getter)) {}

    KCacheGroup &operator=(KCacheGroup &&other);

    // 获取指定 key 的缓存值。先查询当前缓存组的本地 LRU 缓存，未命中时通过 load 加载数据并回填本地缓存。
    ByteViewOptional get(const std::string &key);

    bool set(const std::string &key, ByteView b);

    bool deleteByKey(const std::string &key);

    // 处理来自其他节点的失效请求。
    bool invalidateFromPeer(const std::string &key);

private:

    // 处理本地缓存未命中，调用 getter 获取数据，并写入本地缓存。
    // TODO 当前暂时没有 PeerPicker 的语义，后续需添加并通过 SingleFlight 合并同一 key 的并发加载请求。
    ByteViewOptional load(const std::string &key);


    std::unique_ptr<LRUCache> m_cache;

    std::string m_name;

    std::atomic<bool> m_isClose{false};

    DataGetter m_getter;

    SingleFlight m_loader;

    GroupStatus m_status;
};


#endif
