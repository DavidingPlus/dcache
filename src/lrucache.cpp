#include "lrucache.h"


ByteView ::ByteView(const std::string &str)
{
    m_data.resize(str.size());
    std::copy(str.begin(), str.end(), m_data.begin());
}

// TODO
ByteViewOptional LRUCache::get(const std::string &key)
{
    return {};
}

void LRUCache::set(const std::string &key, const ByteView &)
{
}

void LRUCache::deleteByKey(const std::string &key)
{
}

void LRUCache::removeOldest()
{
}
