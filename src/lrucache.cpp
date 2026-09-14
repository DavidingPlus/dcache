#include "lrucache.h"


ByteView::ByteView(const std::string &str)
{
    m_data.resize(str.size());
    std::copy(str.begin(), str.end(), m_data.begin());
}

ByteViewOptional LRUCache::get(const std::string &key)
{
    std::unique_lock<std::mutex> lock(m_mtx);

    // 找不到缓存。
    if (m_cache.end() == m_cache.find(key)) return std::nullopt;

    // 找到缓存，并修改 m_cache 和 m_list。
    ListElementIter &elemIter = m_cache[key];
    auto [k, value] = *elemIter;

    m_list.erase(elemIter);
    m_list.emplace_front(key, value);

    m_cache[key] = m_list.begin();


    return value;
}

void LRUCache::set(const std::string &key, const ByteView &value)
{
    std::unique_lock<std::mutex> lock(m_mtx);

    // 找到缓存，将其删除。
    if (m_cache.end() != m_cache.find(key))
    {
        ListElementIter &elemIter = m_cache[key];
        m_bytes += value.len() - elemIter->m_value.len();
        m_list.erase(elemIter);
    }
    // 找不到缓存。
    else
    {
        m_bytes += key.size() + value.len();
    }

    // 插入新 value。
    m_list.emplace_front(key, value);
    m_cache[key] = m_list.begin();

    // 当 LRUCache 中还有缓存时，如果此时 LRUCache 中的容量超过规定大小（不能为 0，0 代表不受限制），就不断将最久未使用的缓存淘汰。
    while (0 != m_maxBytes && m_bytes > m_maxBytes && !m_list.empty()) removeOldest();
}

void LRUCache::deleteByKey(const std::string &key)
{
    std::unique_lock<std::mutex> lock(m_mtx);

    if (m_cache.end() == m_cache.find(key)) return;

    ListElementIter &elemIter = m_cache[key];
    auto [_, value] = *elemIter;

    m_list.erase(elemIter);
    m_cache.erase(key);
    m_bytes -= key.size() + value.len();

    if (m_evictedFunc) m_evictedFunc(key, value);
}

void LRUCache::removeOldest()
{
    if (m_list.empty()) return;

    auto [key, value] = m_list.back();

    m_list.pop_back();
    m_cache.erase(key);
    m_bytes -= key.size() + value.len();

    if (m_evictedFunc) m_evictedFunc(key, value);
}
