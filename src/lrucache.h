#ifndef _KCACHE_LRUCACHE_H_
#define _KCACHE_LRUCACHE_H_

#include <vector>
#include <string>
#include <optional>
#include <functional>
#include <list>
#include <mutex>


struct ByteView
{
    ByteView(const std::string &str);

    int64_t len() const { return m_data.size(); }

    std::string toString() const { return std::string(m_data.begin(), m_data.end()); }


    // 底层按照字节存储数据。
    std::vector<char> m_data;
};

using ByteViewOptional = std::optional<ByteView>;


// LRU 链表中存储的一个缓存项，由键和对应的字节值组成。
struct Entry
{
    Entry(std::string k, const ByteView &v) : m_key(std::move(k)), m_value(v) {}

    bool operator==(const Entry &entry) const { return m_key == entry.m_key && m_value.toString() == entry.m_value.toString(); }


    // 缓存键，用于在哈希表 m_cache 中定位对应链表节点。
    std::string m_key;

    // 缓存值，统一使用 ByteView 保存。
    ByteView m_value;
};


class LRUCache
{

    // 淘汰回调函数类型。当缓存项因为 deleteByKey 被显式删除，或因为容量超限被 removeOldest 淘汰时，会把对应的 key 和 value 传给该回调，便于上层做统计、写回或清理等操作。
    using EvictedFunc = std::function<void(std::string, ByteView)>;

    // 缓存项在双向链表 m_list 中的位置。m_cache 通过这个迭代器可以从 key 直接跳到对应链表节点。
    using ListElementIter = std::list<Entry>::iterator;

public:

    // 构造一个按字节容量管理的 LRU 缓存。
    // maxBytes：表示缓存最多占用的字节数；按设计约定，0 表示不启用容量淘汰。
    // evictedFunc：是可选淘汰回调，不传时内部只删除数据，不通知上层。
    LRUCache(int maxBytes, const EvictedFunc &evictedFunc = nullptr) : m_maxBytes(maxBytes), m_evictedFunc(evictedFunc) {}

    // 查询缓存。如果 key 不存在，返回空的 ByteViewOptional；如果 key 存在，返回对应 value，并把该项移动到链表头部，表示最近被使用。
    ByteViewOptional get(const std::string &key);

    // 插入或更新缓存。新写入的项会放在链表头部；如果更新已有 key，需要调整字节统计。写入后如果总占用超过 m_maxBytes，则从链表尾部淘汰最久未使用的项。
    void set(const std::string &key, const ByteView &value);

    // 删除指定 key。删除时会同时清理哈希表和链表，并调用淘汰回调通知上层。
    void deleteByKey(const std::string &key);

    // 淘汰链表尾部的缓存项，也就是当前最久未使用的项。
    void removeOldest();


private:

    // 当前缓存的近似字节数。统计口径为每个缓存项的 key.size() + value.len()，不包含链表节点、哈希表节点等容器本身的开销。
    int64_t m_bytes = 0;

    // 缓存允许占用的最大字节数。超过该容量后，Set 需要淘汰旧数据；0 表示不启用容量淘汰。
    int64_t m_maxBytes;

    // 淘汰回调。显式删除和容量淘汰都会触发；为空表示没有额外处理。
    EvictedFunc m_evictedFunc;

    // 哈希表：key 到链表节点迭代器的映射。通过它可以 O(1) 找到某个 key 在双向链表中的位置，避免 get、set、deleteByKey 时遍历链表。
    std::unordered_map<std::string, ListElementIter> m_cache;

    // 双向链表：维护缓存项的使用顺序。链表头部表示最近使用，链表尾部表示最久未使用；容量不足时从尾部淘汰。
    std::list<Entry> m_list;

    // 互斥锁，保证多线程环境下缓存操作的线程安全。m_cache、m_list、m_bytes 都属于共享可变状态，公开接口访问时需要加锁保护。
    std::mutex m_mtx;
};


#endif
