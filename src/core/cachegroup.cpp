#include "cachegroup.h"

#include <spdlog/spdlog.h>


KCacheGroup &KCacheGroup::operator=(KCacheGroup &&other)
{
    m_cache = std::move(other.m_cache);
    m_name = std::move(other.m_name);
    m_getter = std::move(other.m_getter);
    return *this;
}

ByteViewOptional KCacheGroup::get(const std::string &key)
{
    if (m_isClose)
    {
        spdlog::error("Cache group [{}] is closed!!!", m_name);
        return std::nullopt;
    }

    if (key.empty())
    {
        spdlog::warn("The key [{}] is empty, you can't get a empty key from cache group", key);
        return std::nullopt;
    }


    // 先从本地缓存中获取，未命中去远端加载。
    auto res = m_cache->get(key);
    if (res)
    {
        // 本地命中缓存次数 +1。
        ++m_status.m_localHits;
        return res;
    }
    else
    {
        // 本地未命中缓存次数 +1。
        ++m_status.m_localMisses;
        return load(key);
    }
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
    auto res = m_loader.Do(key, [&]() -> ByteViewOptional
                           {
                               spdlog::info("Try to load key [{}] from local", key);

                               // 通过 getter 从数据源获取。
                               auto val = m_getter(key);
                               if (!val) return std::nullopt;

                               ++m_status.m_localHits;
                               return val; //
                           });

    if (!res)
    {
        spdlog::error("Failed to load data for key: {}", key);
        return std::nullopt;
    }

    m_cache->set(key, res.value());

    // TODO 记录加载时间。


    return res;
}
