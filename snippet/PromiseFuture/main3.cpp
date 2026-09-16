#include <chrono>
#include <future>
#include <iostream>
#include <thread>

using namespace std::chrono_literals;


int main()
{
    std::promise<int> promise;
    std::future<int> future = promise.get_future();

    std::thread worker([promise = std::move(promise)]() mutable
                       {
                           std::this_thread::sleep_for(1500ms);
                           std::cout << "worker: set value" << std::endl;
                           promise.set_value(42); //
                       });

    // 最多等待 200ms；结果尚未准备好，因此得到 timeout。
    if (std::future_status::timeout == future.wait_for(200ms)) std::cout << "wait_for: timeout" << std::endl;

    // 等待到指定的时间点；结果仍未准备好，因此再次得到 timeout。
    auto deadline = std::chrono::steady_clock::now() + 300ms;
    if (std::future_status::timeout == future.wait_until(deadline)) std::cout << "wait_until: timeout" << std::endl;

    // wait() 没有超时限制，直到工作线程设置结果才返回。
    std::cout << "wait: waiting" << std::endl;
    future.wait();
    std::cout << "wait: ready" << std::endl;

    std::cout << "main: value = " << future.get() << std::endl;
    worker.join();
}
