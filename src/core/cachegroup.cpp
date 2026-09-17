#include "cachegroup.h"


KCacheGroup &KCacheGroup::operator=(KCacheGroup &&other)
{
    m_cache = std::move(other.m_cache);
    m_name = std::move(other.m_name);
    m_getter = std::move(other.m_getter);
    return *this;
}

ByteViewOptional KCacheGroup::get(const std::string &key)
{
}

bool KCacheGroup::set(const std::string &key, ByteView b)
{
}

bool KCacheGroup::deleteByKey(const std::string &key)
{
}

bool KCacheGroup::invalidateFromPeer(const std::string &key)
{
}

KCacheGroup &KCacheGroup::MakeCacheGroup(const std::string &name, int64_t bytes, DataGetter getter)
{
}

KCacheGroup *KCacheGroup::GetCacheGroup(const std::string &name)
{
}

ByteViewOptional KCacheGroup::load(const std::string &key)
{
}

ByteViewOptional KCacheGroup::loadData(const std::string &key)
{
}
