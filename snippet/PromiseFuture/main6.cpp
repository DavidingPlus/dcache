#include <chrono>
#include <future>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace std::chrono_literals;


int main()
{
    std::promise<std::string> promise;
    std::shared_future<std::string> sharedFuture = promise.get_future().share();
    std::mutex mtx;
    std::vector<std::thread> readers;

    for (int i = 0; i < 3; ++i)
    {
        // shared_future 可以复制，因此每个线程持有自己的副本。
        readers.emplace_back([sharedFuture, i, &mtx]
                             {
                                 // shared_future 被按值捕获，每个线程都有自己的副本；多个副本可以安全地同时调用 get()，并且 get() 返回的是共享状态中结果的只读引用。
                                 const std::string &value = sharedFuture.get();

                                 // mtx 只用于保护 std::cout，避免多个读取线程同时输出时，一行日志被交错拼接。它不用于保护 shared_future。
                                 std::lock_guard<std::mutex> lock(mtx);
                                 std::cout << "reader " << i << ": " << value << std::endl; //
                             });
    }

    std::this_thread::sleep_for(300ms);
    std::cout << "main: set value" << std::endl;
    promise.set_value("shared result");

    for (std::thread &reader : readers) reader.join();
}
