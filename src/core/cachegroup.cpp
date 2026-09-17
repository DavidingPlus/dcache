#include "cachegroup.h"


KCacheGroup::KCacheGroup(KCacheGroup &&other)
{
}

KCacheGroup &KCacheGroup::operator=(KCacheGroup &&other)
{
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

ByteViewOptional KCacheGroup::load(const std::string &key)
{
}

ByteViewOptional KCacheGroup::loadData(const std::string &key)
{
}
