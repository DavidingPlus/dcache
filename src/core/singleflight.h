#ifndef _DCACHE_SINGLEFLIGHT_H_
#define _DCACHE_SINGLEFLIGHT_H_

#include <future>
#include <optional>
#include <unordered_map>
#include <memory>

#include "lrucache.h"


// 缓存三大问题：击穿、雪崩、穿透。
// 1. 缓存击穿：某个热点 key 在缓存过期瞬间，同时有大量请求访问这个 key，导致所有请求都落到数据库上，造成数据库瞬时压力过大。
// 2. 缓存雪崩：大量缓存 key 同时失效，导致所有请求都落到数据库上，引起数据库压力激增甚至崩溃。
// 3. 缓存穿透：查询不存在的数据，导致每次请求都绕过缓存直接访问数据库。

// SingleFlight 是一种用于合并并发请求的设计模式/机制，主要用于防止重复的昂贵操作（如数据库查询、HTTP 请求等）被不必要地多次执行。它的核心思想是合并重复请求：当多个并发请求同时访问同一个资源（如缓存）时，只让第一个请求执行实际操作，其他请求等待该操作完成后直接复用结果。
// 本项目使用 SingleFlight 合并并发请求，只允许一个请求访问数据源，防止缓存击穿。通过线程安全的缓存机制，确保对同一个键（key）的多次并发请求只会执行一次实际操作，其他请求会等待并复用结果。
// 注意：它处理的是所有线程当前正在执行的 func，而不是保存查询结果的缓存；只有执行时间重叠的同 key 请求才会被合并。首个请求负责执行 func，后续请求复用其进行中的 Call；func 结束后立即删除 Call，使下一轮请求能够重新执行并获取最新结果。因此 SingleFlight 主要用于抑制短时间内突发的重复操作，通常与负责长期保存结果的缓存配合使用。
class SingleFlight
{

    using Result = std::optional<ByteView>;

    using Func = std::function<Result()>;

public:

    Result Do(const std::string &key, Func func);


private:


    // 对于 Call 结构体：
    // 1. m_prom：std::promise，用于设置异步操作的结果。
    // 2. m_fut：std::shared_future，共享的 future 对象，允许多个线程等待结果。
    // 3. 通过 prom.get_future().share() 创建共享的 future，使得多个线程可以安全等待。
    // 其工作流程可类比如下：
    // Thread 1: Get("user:123") → 创建 Call → 执行 func() → 设置结果 → 清理。
    // Thread 2: Get("user:123") → 发现存在 Call → 等待结果。
    // Thread 3: Get("user:123") → 发现存在 Call → 等待结果。
    // Thread 4: Get("user:456") → 创建新 Call → 独立执行。
    // 简单来说，SingleFlight 将"多个相同请求"转换为"一次执行+多次共享"。
    struct Call
    {
        std::promise<Result> m_prom;

        std::shared_future<Result> m_fut = m_prom.get_future().share();
    };

    // 互斥锁，用于保护 m_map。
    std::mutex m_mtx;

    // 进行中调用表：仅在 func 尚未结束时保留 key 到 Call 的映射，完成后由 leader 删除该条目。
    std::unordered_map<std::string, std::shared_ptr<Call>> m_map;
};


#endif
