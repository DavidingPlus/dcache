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


    std::vector<char> m_data;
};

using ByteViewOptional = std::optional<ByteView>;


struct Entry
{
    Entry(std::string k, const ByteView &v) : m_key(std::move(k)), m_value(v) {}

    bool operator==(const Entry &entry) const { return m_key == entry.m_key && m_value.toString() == entry.m_value.toString(); }


    std::string m_key;

    ByteView m_value;
};


class LRUCache
{

    using EvictedFunc = std::function<void(std::string, ByteView)>;
    using ListElementIter = std::list<Entry>::iterator;

public:

    LRUCache(int max_bytes, const EvictedFunc &evicted_func = nullptr) : m_maxBytes(max_bytes), m_evictedFunc(evicted_func) {}

    ByteViewOptional Get(const std::string &key);

    void Set(const std::string &key, const ByteView &);

    void Delete(const std::string &key);

    void RemoveOldest();


private:

    int64_t m_bytes = 0;

    int64_t m_maxBytes;

    EvictedFunc m_evictedFunc;

    std::unordered_map<std::string, ListElementIter> m_cache;

    std::list<Entry> m_list;

    std::mutex m_mtx;
};


#endif
