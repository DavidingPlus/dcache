#include "cachegroupregistry.h"

#include <stdexcept>
#include <utility>

#include <spdlog/spdlog.h>


DCacheGroupRegistry &DCacheGroupRegistry::Instance()
{
    // 函数内静态对象在 C++11 及之后保证线程安全地初始化。
    static DCacheGroupRegistry registry;
    return registry;
}

DCacheGroup &DCacheGroupRegistry::MakeCacheGroup(const std::string &name, int64_t bytes, DataGetter getter)
{
    if (name.empty())
    {
        spdlog::critical("no cache group name!");
        throw std::invalid_argument("cache group name must not be empty");
    }

    if (!getter)
    {
        spdlog::critical("no cache group getter function!");
        throw std::invalid_argument("cache group getter must not be empty");
    }


    std::lock_guard lock(m_mtx);

    // 使用堆对象保存缓存组，避免注册表扩容或 DCacheGroup 的移动语义影响已返回的对象地址。
    auto [iter, inserted] = m_cacheGroups.emplace(
        name, std::make_unique<DCacheGroup>(name, bytes, std::move(getter)));
    if (!inserted) throw std::invalid_argument("cache group already exists: " + name);


    return *iter->second;
}

DCacheGroup *DCacheGroupRegistry::GetCacheGroup(const std::string &name)
{
    std::lock_guard lock(m_mtx);

    auto iter = m_cacheGroups.find(name);


    return m_cacheGroups.end() == iter ? nullptr : iter->second.get();
}
