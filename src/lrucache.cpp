#include "lrucache.h"


ByteView ::ByteView(const std::string &str)
{
    m_data.resize(str.size());
    std::copy(str.begin(), str.end(), m_data.begin());
}

// TODO
ByteViewOptional LRUCache::Get(const std::string &key)
{
    return {};
}

void LRUCache::Set(const std::string &key, const ByteView &)
{
}

void LRUCache::Delete(const std::string &key)
{
}

void LRUCache::RemoveOldest()
{
}
