#include <future>
#include <iostream>
#include <system_error>


int main()
{
    std::future<int> future;

    // promise 未设置值或异常便被析构。
    {
        std::promise<int> promise;
        future = promise.get_future();

        std::cout << "producer: leave scope without a value" << std::endl;
    }

    try
    {
        future.get();
    }
    catch (const std::future_error &ex)
    {
        if (std::make_error_code(std::future_errc::broken_promise) == ex.code())
        {
            std::cout << "main: broken_promise" << std::endl;
        }
        else
        {
            std::cout << "main: unexpected future_error: " << ex.what() << std::endl;
        }
    }
}
