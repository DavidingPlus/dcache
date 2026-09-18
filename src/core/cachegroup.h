#ifndef _DCACHE_CACHE_GROUP_H_
#define _DCACHE_CACHE_GROUP_H_

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


class DCacheGroup
{

public:

    DCacheGroup() = default;

    DCacheGroup(std::string name, int64_t bytes, DataGetter getter) : m_cache(std::make_unique<LRUCache>(bytes)), m_name(name), m_getter(getter) {}

    DCacheGroup(const DCacheGroup &) = delete;

    DCacheGroup &operator=(const DCacheGroup &other) = delete;

    DCacheGroup(DCacheGroup &&other) : m_cache(std::move(other.m_cache)), m_name(std::move(other.m_name)), m_getter(std::move(other.m_getter)) {}

    DCacheGroup &operator=(DCacheGroup &&other);

    // 获取指定 key 的缓存值。先查询当前缓存组的本地 LRU 缓存，未命中时通过 load 加载数据并回填本地缓存。
    ByteViewOptional get(const std::string &key);

    // 将 key 和 value 写入当前缓存组的本地 LRU 缓存；只影响当前节点，不负责向其他节点同步。
    bool set(const std::string &key, ByteView b);

    // 删除当前缓存组本地 LRU 中的指定 key；不会删除数据源中的原始数据。
    bool deleteByKey(const std::string &key);

    // 处理来自其他节点的失效请求：当前节点收到其他缓存节点发来的“这个 key 已失效，请删除本地副本”的通知。只删除当前节点的本地副本，不重新加载数据，也不继续向其他节点转发。
    bool invalidateFromPeer(const std::string &key);

private:

    // 处理本地缓存未命中，调用 getter 获取数据，并写入本地缓存。
    // TODO 当前暂时没有 PeerPicker 的语义，后续需添加并通过 SingleFlight 合并同一 key 的并发加载请求。
    ByteViewOptional load(const std::string &key);


    // 当前缓存组在本节点上的本地 LRU 缓存。
    std::unique_ptr<LRUCache> m_cache;

    // 缓存组名称，用于标识当前逻辑缓存组。
    std::string m_name;

    // 缓存组关闭标志；关闭后不再处理缓存请求。
    std::atomic<bool> m_isClose{false};

    // 数据加载回调，在本地缓存未命中时从数据库、文件或其他数据源获取数据。
    DataGetter m_getter;

    // 合并同一个 key 的并发加载请求，避免缓存击穿时重复访问数据源。
    SingleFlight m_loader;

    // 缓存组运行状态统计信息。
    GroupStatus m_status;
};


#endif
