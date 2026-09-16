#include <future>
#include <iostream>


int main()
{
    std::promise<int> promise;
    std::future<int> future = promise.get_future();
    promise.set_value(7);

    std::cout << "future: first get = " << future.get() << std::endl;

    try
    {
        future.get();
    }
    catch (const std::future_error &)
    {
        std::cout << "future: second get -> future_error" << std::endl;
    }

    std::promise<int> sharedPromise;
    std::shared_future<int> sharedFuture = sharedPromise.get_future().share();
    sharedPromise.set_value(42);

    std::cout << "shared_future: first get = " << sharedFuture.get() << std::endl;
    std::cout << "shared_future: second get = " << sharedFuture.get() << std::endl;
}
