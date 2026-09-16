#include <chrono>
#include <future>
#include <iostream>
#include <thread>

using namespace std::chrono_literals;


int main()
{
    // 1. 主线程创建 promise 和与之关联的 future。
    std::promise<int> promise;
    std::future<int> future = promise.get_future();

    // 2. promise 不可复制，因此移动到工作线程。
    std::thread worker([promise = std::move(promise)]() mutable
                       {
                           std::this_thread::sleep_for(2s);

                           std::cout << "worker: set value" << std::endl;
                           promise.set_value(42); //
                       });

    // 3. 结果尚未准备好，get() 会阻塞主线程。
    std::cout << "main: waiting for result" << std::endl;
    int value = future.get();

    // 4. 工作线程设置结果后，get() 返回。
    std::cout << "main: value = " << value << std::endl;

    worker.join();
}
