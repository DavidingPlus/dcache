# `promise`、`future` 与 `shared_future` 学习笔记

这三个类型用于在线程之间传递“某个未来才会产生的结果”。它们本身不是线程，而是一套线程同步和结果传递机制。

可以先记住一句话：

> `promise` 负责写入结果，`future` 负责读取一次结果，`shared_future` 负责让多个线程读取同一个结果。

## 1. 为什么需要它们

假设主线程启动一个工作线程，工作线程需要一段时间才能计算出结果：

```text
主线程：启动任务 ───────────── 等待结果 ── 使用结果
                         ▲
工作线程：        执行任务 ── 写入结果
```

如果只使用普通变量，就需要自己处理：

- 结果是否已经准备好；
- 主线程如何等待；
- 如何避免数据竞争；
- 工作线程发生异常时如何通知主线程。

`promise` 和 `future` 将这些事情封装成了一个“共享状态（shared state）”。

## 2. 三者之间的关系

它们通常通过下面的方式连接：

```cpp
std::promise<int> promise;
std::future<int> future = promise.get_future();
```

逻辑关系是：

```text
生产者线程                         消费者线程

promise.set_value(42)  ────────>  future.get() == 42
```

`promise` 和 `future` 通过内部共享状态连接，但它们不是同一个对象：

- `promise` 持有“设置结果”的能力；
- `future` 持有“等待并取得结果”的能力；
- 结果设置前，`future.get()` 会阻塞；
- 结果设置后，`future.get()` 才会返回或抛出异常。

## 3. `std::promise`

### 3.1 基本用途

`std::promise<T>` 表示一个将来要产生 `T` 类型结果的对象。

常用接口如下：

```cpp
std::future<T> get_future();
void set_value(const T& value);
void set_value(T&& value);
void set_exception(std::exception_ptr exception);
```

示例：

```cpp
std::promise<int> promise;

// 未来某个时刻设置结果
promise.set_value(42);
```

### 3.2 `promise` 通常要移动到工作线程

`std::promise` 不允许复制，但可以移动。因此经常使用移动捕获：

```cpp
#include <chrono>
#include <future>
#include <iostream>
#include <thread>

int main() {
    std::promise<int> promise;
    std::future<int> future = promise.get_future();

    std::thread worker([
        promise = std::move(promise)
    ]() mutable {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        promise.set_value(42);
    });

    // 结果尚未产生时，这里会阻塞
    std::cout << future.get() << '\n';

    worker.join();
}
```

这里的执行过程是：

1. 主线程创建 `promise`；
2. 主线程通过 `get_future()` 得到对应的 `future`；
3. `promise` 被移动到工作线程；
4. 工作线程计算完成后调用 `set_value(42)`；
5. 主线程中的 `future.get()` 被唤醒并得到 `42`。

### 3.3 一个 `promise` 只能设置一次结果

下面的代码是错误的：

```cpp
promise.set_value(1);
promise.set_value(2); // 错误：同一个 promise 不能重复设置结果
```

第二次设置通常会抛出 `std::future_error`。因此，一个 `promise` 对应的是“一次性结果”，而不是可以反复更新的变量。

## 4. `std::future`

### 4.1 `get()` 会等待结果

```cpp
std::future<int> future = promise.get_future();
int value = future.get();
```

如果 `promise` 还没有设置结果，`get()` 会阻塞当前线程；如果已经设置结果，`get()` 会立即返回。

### 4.2 `future::get()` 只能调用一次

```cpp
int value = future.get();
int again = future.get(); // 错误：future 的结果只能取一次
```

普通 `future` 是“独占式”的结果读取者。调用 `get()` 后，它通常不再持有共享状态，`future.valid()` 会变成 `false`。

如果只是想查看结果是否已经准备好，可以使用：

```cpp
future.wait();
future.wait_for(std::chrono::milliseconds(10));
future.wait_until(deadline);
```

其中：

- `wait()` 一直等待；
- `wait_for()` 最多等待指定时长；
- `wait_until()` 等待到指定时间点。

### 4.3 用 `wait_for()` 做超时检查

```cpp
using namespace std::chrono_literals;

if (future.wait_for(100ms) == std::future_status::ready) {
    int value = future.get();
} else {
    // 超时处理
}
```

注意：超时只表示“这次等待没有等到结果”，不会自动取消后台任务，也不会销毁 `promise`。

## 5. `std::shared_future`

### 5.1 为什么需要 `shared_future`

如果同一个结果需要被多个线程读取，普通 `future` 不适合，因为它只能调用一次 `get()`。

`std::shared_future<T>` 的特点是：

- 可以复制；
- 多个副本共享同一个共享状态；
- 多个线程都可以调用 `get()`；
- 每个线程都能得到同一个结果。

创建方式：

```cpp
std::promise<int> promise;
std::shared_future<int> shared = promise.get_future().share();
```

调用 `.share()` 后，原来的 `future` 不再有效，结果由 `shared_future` 持有。

### 5.2 多个线程读取同一个结果

```cpp
#include <future>
#include <iostream>
#include <thread>

int main() {
    std::promise<int> promise;
    std::shared_future<int> shared = promise.get_future().share();

    std::thread first([shared] {
        std::cout << "first: " << shared.get() << '\n';
    });

    std::thread second([shared] {
        std::cout << "second: " << shared.get() << '\n';
    });

    promise.set_value(42);

    first.join();
    second.join();
}
```

`shared` 按值捕获是安全的，因为 `shared_future` 本身可以复制。两个线程等待的是同一个共享状态。

## 6. 异常如何传递

生产者线程中的异常不能自动被另一个线程的 `try/catch` 捕获。需要通过 `promise.set_exception()` 将异常放入共享状态：

```cpp
#include <exception>
#include <future>
#include <iostream>
#include <stdexcept>

int main() {
    std::promise<int> promise;
    std::future<int> future = promise.get_future();

    try {
        throw std::runtime_error("load failed");
    } catch (...) {
        promise.set_exception(std::current_exception());
    }

    try {
        future.get(); // 这里会重新抛出 runtime_error
    } catch (const std::exception& ex) {
        std::cout << ex.what() << '\n';
    }
}
```

常见的工作线程写法是：

```cpp
std::thread worker([
    promise = std::move(promise)
]() mutable {
    try {
        int result = do_work();
        promise.set_value(result);
    } catch (...) {
        promise.set_exception(std::current_exception());
    }
});
```

这样，调用 `future.get()` 的线程就能感知工作线程的失败。

## 7. `promise` 被意外销毁时会发生什么

如果 `promise` 在没有设置值、也没有设置异常的情况下被销毁，对应的 `future.get()` 通常会抛出 `std::future_error`，错误类型为 `broken_promise`。

这比永久阻塞更安全，但仍然说明生产者代码没有正确完成结果通知。

## 8. 建议的学习顺序

可以按下面的顺序编写实验：

1. 单线程中创建 `promise` 和 `future`，手动调用 `set_value()`；
2. 使用一个工作线程设置结果，主线程调用 `future.get()`；
3. 分别使用 `wait()`、`wait_for()` 和 `wait_until()` 观察等待行为与超时状态；
4. 使用 `set_exception()` 和 `future.get()` 验证异常传递；
5. 让未设置结果的 `promise` 离开作用域，验证 `broken_promise`；
6. 将 `future` 转成 `shared_future`，让多个线程同时读取同一个结果；
7. 验证普通 `future::get()` 只能调用一次，而 `shared_future::get()` 可以调用多次。

编译单个示例时，C++17 下通常需要链接线程库：

```bash
g++ -std=c++17 -pthread example.cpp -o example
```

## 9. 最容易记混的几点

| 类型 | 主要职责 | 是否可复制 | `get()` 次数 |
|---|---|---:|---:|
| `std::promise<T>` | 设置结果或异常 | 否 | 不适用 |
| `std::future<T>` | 独占地获取结果 | 否 | 一次 |
| `std::shared_future<T>` | 共享地获取结果 | 是 | 多次 |

再用一句话总结：

> `promise` 是生产者的出口，`future` 是单个消费者的入口，`shared_future` 是多个消费者共享的入口。

