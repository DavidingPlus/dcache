#include <exception>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>


int doWork() { throw std::runtime_error("load failed"); }


int main()
{
    std::promise<int> promise;
    std::future<int> future = promise.get_future();

    std::thread worker([promise = std::move(promise)]() mutable
                       {
                           try
                           {
                               int result = doWork();
                               promise.set_value(result);
                           }
                           catch (...)
                           {
                               // 将工作线程当前处理的异常存入共享状态。
                               std::cout << "worker: store exception" << std::endl;
                               promise.set_exception(std::current_exception());
                           } //
                       });

    try
    {
        future.get();
    }
    catch (const std::exception &ex)
    {
        // get() 在当前线程重新抛出工作线程存入的异常。
        std::cout << "main: " << ex.what() << std::endl;
    }

    worker.join();
}
