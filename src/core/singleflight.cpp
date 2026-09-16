#include "singleflight.h"

#include <exception>


SingleFlight::Result SingleFlight::Do(const std::string &key, SingleFlight::Func func)
{
    // m_map 是所有线程共享的“进行中调用表”。先加锁，使检查 key 和插入新 Call 成为一个原子步骤，避免多个线程同时成为同一 key 的 leader。
    std::unique_lock<std::mutex> lock(m_mtx);

    // 有正在进行中的调用。
    if (m_map.end() != m_map.find(key))
    {
        auto existingCall = m_map[key];

        // 已经取得 Call 的 shared_ptr，后续无需访问 m_map。等待结果时不能持有组锁，否则会阻塞其他 key 的请求，也会阻塞 leader 完成后的清理。
        lock.unlock();

        // 直接阻塞等待 future 的结果。
        return existingCall->m_fut.get();
    }

    // 创建新的调用对象。
    auto newCall = std::make_shared<Call>();
    m_map[key] = newCall;

    // 新 Call 已经登记，其他同 key 请求会作为 follower 等待它。这里释放组锁后，再执行可能耗时的 func，保证不同 key 的请求仍可并发地查表、登记或等待。
    lock.unlock();

    // 执行用户函数并设置 promise。
    Result val;

    try
    {
        val = func();
        newCall->m_prom.set_value(val);
    }
    catch (...)
    {
        // 让所有已加入该 Call 的 follower 从 future.get() 收到同一个异常，而不是永久等待。
        newCall->m_prom.set_exception(std::current_exception());

        // 即使 func 失败也必须删除进行中记录，后续同 key 请求才能重新执行。
        lock.lock();
        m_map.erase(key);

        throw;
    }

    // 删除共享的进行中记录前必须重新获取组锁。unique_lock 在函数返回时自动解锁。
    lock.lock();
    m_map.erase(key);


    return val;
}
