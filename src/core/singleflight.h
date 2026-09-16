#ifndef _KCACHE_SINGLEFLIGHT_H_
#define _KCACHE_SINGLEFLIGHT_H_

#include <future>
#include <optional>
#include <unordered_map>
#include <memory>

#include "lrucache.h"


// 缓存三大问题：击穿、雪崩、穿透。
// 1. 缓存击穿：某个热点 key 在缓存过期瞬间，同时有大量请求访问这个 key，导致所有请求都落到数据库上，造成数据库瞬时压力过大。
// 2. 缓存雪崩：大量缓存 key 同时失效，导致所有请求都落到数据库上，引起数据库压力激增甚至崩溃。
// 3. 缓存穿透：查询不存在的数据，导致每次请求都绕过缓存直接访问数据库。

// SingleFlight 是一种用于合并并发请求的设计模式/机制，主要用于防止重复的昂贵操作（如数据库查询、HTTP 请求等）被不必要地多次执行。
// Singleflight 的核心思想是合并重复请求：当多个并发请求同时访问同一个资源（如缓存）时，只让第一个请求执行实际操作，其他请求等待该操作完成后直接复用结果。
// 这个项目中使用 SingleFlight 合并并发请求，只允许一个请求访问数据源，防止缓存击穿。通过线程安全的缓存机制，确保对同一个键（key）的多次并发请求只会执行一次实际操作，其他请求会等待并复用结果。
class SingleFlight
{

    using Result = std::optional<ByteView>;

    using Func = std::function<Result()>;

public:

    Result Do(const std::string &key, Func func);


private:

    struct Call
    {
        std::promise<Result> m_prom;

        std::shared_future<Result> m_fut = m_prom.get_future().share();
    };

    std::mutex m_mtx;

    std::unordered_map<std::string, std::shared_ptr<Call>> m_map;
};


#endif
