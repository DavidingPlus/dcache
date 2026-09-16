#include <future>
#include <iostream>


int main()
{
    // 1. 创建 promise。
    std::promise<int> promise;

    // 2. 从 promise 获取与它关联的 future。
    std::future<int> future = promise.get_future();

    // 3. promise 向共享状态写入结果。
    promise.set_value(42);

    // 4. future 从共享状态取得结果。
    int value = future.get();

    std::cout << "value = " << value << std::endl;
}
