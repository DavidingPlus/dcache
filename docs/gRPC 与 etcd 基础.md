# gRPC 与 etcd 基础

本文记录分布式缓存中 gRPC、Protocol Buffers、`.proto`、`protoc`、gRPC 服务基类和 etcd 的基本语义。

## 1. 先看整体分工

分布式缓存通常同时需要解决以下几类问题：

```text
有哪些缓存节点？                 → etcd 服务注册与发现
某个 key 应该去哪？               → 一致性哈希
请求和响应的数据如何定义、编码？   → Protocol Buffers
如何调用目标节点？                 → gRPC
网络数据如何传输？                 → HTTP/2（标准 gRPC 通常使用）
```

可以把一次远程缓存调用抽象成下面的过程：

```text
客户端业务对象
    │
    │ Protobuf 序列化
    ▼
二进制消息
    │
    │ gRPC 调用与 HTTP/2 传输
    ▼
目标节点的 gRPC Server
    │
    │ Protobuf 反序列化
    ▼
服务端业务对象
```

它们的职责不同，不能混为一谈：

| 组件 | 主要职责 | 是否保存缓存值 |
| --- | --- | --- |
| etcd | 保存节点地址、服务注册信息和配置，并通知变化 | 否 |
| 一致性哈希 | 将 key 映射到负责的缓存节点 | 否 |
| `.proto` | 描述消息字段和 RPC 服务契约 | 否 |
| Protobuf | 将消息对象编码为紧凑的二进制数据，并负责解码 | 否 |
| gRPC | 定义和执行远程过程调用，传输请求和响应 | 否 |
| HTTP/2 | 为标准 gRPC 提供底层网络传输 | 否 |
| gRPC Server | 接收 RPC，并调用本节点的缓存逻辑 | 间接使用 |
| CacheGroup / LRU | 执行本地缓存读写和淘汰 | 是 |

### 1.1 gRPC 和 Protobuf 的关系

gRPC 和 Protobuf 经常一起出现，但它们解决的是不同问题：

```text
Protobuf：消息长什么样？如何序列化和反序列化？
gRPC：    如何调用远程方法？如何把请求发到服务端？
```

同一份 `.proto` 文件通常同时包含两类定义：

```proto
message Request {
    string key = 1;
}

service Cache {
    rpc Get(Request) returns (GetResponse);
}
```

其中：

- `message` 定义由 Protobuf 处理，生成 `*.pb.h`、`*.pb.cc`，提供消息类、字段访问、序列化和反序列化；
- `service` 和 `rpc` 定义由 gRPC 代码生成插件处理，通常生成 `*.grpc.pb.h`、`*.grpc.pb.cc`，提供客户端 `Stub` 和服务端 `Service` 基类；
- gRPC 默认使用 Protobuf 作为请求和响应的编码格式，但 Protobuf 本身可以脱离 gRPC 使用；
- Protobuf 不负责建立连接、监听端口、节点发现或远程调用；
- gRPC 也不负责一致性哈希、缓存淘汰或 etcd 服务发现。

因此，“gRPC 是节点之间通信的一种协议”这个说法可以帮助理解，但更准确地说，gRPC 是一套 RPC 框架和通信机制；Protobuf 是它常用的接口描述与消息序列化方案。

### 1.2 `protoc` 和 gRPC 插件的分工

```text
.proto
  │
  ├─ protoc                  → Protobuf 消息类
  │                            *.pb.h / *.pb.cc
  │
  └─ protoc + grpc_cpp_plugin → gRPC 调用代码
                               *.grpc.pb.h / *.grpc.pb.cc
```

只使用 Protobuf 时，可以直接把消息序列化后写入文件、消息队列或自定义 TCP 连接，不需要 gRPC。使用 gRPC 时，则通常同时使用 Protobuf 消息定义和 gRPC 生成的客户端/服务端代码。

## 2. gRPC Server 在缓存系统中的语义

每个缓存节点可以启动一个 gRPC Server，对外暴露缓存操作接口：

```text
客户端 SDK / HTTP Gateway
          │
          │ gRPC Get / Set / Delete / Invalidate
          ▼
缓存节点的 gRPC Server
          │
          ▼
CacheGroupRegistry
          │
          ▼
指定的 CacheGroup
          │
          ▼
本地 LRU / DataGetter
```

gRPC Server 的职责是“接收并分发请求”，不是：

- 决定 key 应该路由到哪个节点；
- 保存所有节点共享的缓存数据；
- 代替 etcd 进行服务发现；
- 把 C++ 的 `DataGetter` 函数通过网络传给其他节点。

请求通常至少需要包含：

```text
group_name
key
value（Set 时需要）
```

其中 `group_name` 用于在目标进程的缓存组注册表中找到对应缓存组。数据加载器应该在每个节点本地配置，因为本地函数不能直接作为 RPC 参数传输。

## 3. `.proto` 文件是什么

`.proto` 是 Protocol Buffers 的接口定义文件，可以把它理解为网络 API 的“跨语言头文件”。它定义：

- 请求和响应消息的字段；
- 字段类型和编号；
- gRPC 服务名称；
- 每个 RPC 方法的输入和输出类型。

一个简化的缓存接口可以写成：

```proto
syntax = "proto3";

package cache;

service Cache {
    rpc Get(Request) returns (GetResponse);
    rpc Set(Request) returns (SetResponse);
    rpc Delete(Request) returns (DeleteResponse);
    rpc Invalidate(Request) returns (InvalidateResponse);
}

message Request {
    string group = 1;
    string key = 2;
    string value = 3;
}

message GetResponse {
    string value = 1;
}
```

`.proto` 只描述接口契约，不包含具体的缓存查询、LRU 淘汰或数据库回源实现。

## 4. `protoc` 编译器是什么

`protoc` 是 Protocol Buffers 编译器，用来把 `.proto` 文件转换成目标语言代码。

仅使用 protobuf 编译器时，通常会生成：

```text
cache.pb.h
cache.pb.cc
```

这些代码包含 `Request`、`GetResponse` 等消息类，以及字段访问、序列化和反序列化逻辑。

生成 gRPC 相关代码时，还要配合 gRPC 的代码生成插件，通常是 `grpc_cpp_plugin`，生成：

```text
cache.grpc.pb.h
cache.grpc.pb.cc
```

这部分代码通常包含：

- 客户端 `Stub`；
- 服务端 `Service` 基类；
- RPC 方法的注册和调用绑定代码。

可以把生成结果简单理解为：

```text
.proto
  │
  ├─ protoc                  → protobuf 消息类
  └─ protoc + grpc_cpp_plugin → gRPC Stub 和 Service
```

## 5. gRPC 服务基类是什么

`.proto` 中的 `service` 定义会生成一个服务端基类。例如：

```proto
service Cache {
    rpc Get(Request) returns (GetResponse);
}
```

生成代码后，通常会有类似下面的类型：

```cpp
class Cache::Service {
public:
    virtual grpc::Status Get(
        grpc::ServerContext *context,
        const Request *request,
        GetResponse *response);
};
```

服务端需要继承这个基类，并实现具体业务：

```cpp
class CacheService final : public Cache::Service {
public:
    grpc::Status Get(
        grpc::ServerContext *context,
        const Request *request,
        GetResponse *response) override
    {
        auto *group = CacheGroupRegistry::Instance()
                          .GetCacheGroup(request->group());

        if (!group) {
            return grpc::Status(
                grpc::StatusCode::NOT_FOUND,
                "cache group not found");
        }

        auto value = group->get(request->key());
        if (!value) {
            return grpc::Status(
                grpc::StatusCode::NOT_FOUND,
                "cache key not found");
        }

        response->set_value(value->toString());
        return grpc::Status::OK;
    }
};
```

这里要区分三个对象：

```text
Service 基类：生成代码提供的服务端接口契约
CacheService：业务方自己实现的服务类
grpc::Server：真正监听端口、接收网络连接的 gRPC 服务器对象
```

客户端则使用生成的 `Stub`：

```cpp
auto channel = grpc::CreateChannel(
    address,
    grpc::InsecureChannelCredentials());

auto stub = Cache::NewStub(channel);
stub->Get(&context, request, &response);
```

因此，服务基类本身不是一个已经运行的服务器，而是要求业务方实现 RPC 方法的接口模板。

## 6. etcd 服务是什么

etcd 是一个基于一致性协议维护的分布式键值存储。在缓存系统中，它通常作为控制平面使用，负责：

- 服务注册；
- 服务发现；
- 节点上下线通知；
- 少量集群配置存储。

etcd 不负责保存缓存的 key-value 数据，也不处理缓存的 LRU 淘汰。

例如，一个缓存节点可以在 etcd 中注册：

```text
/services/cache/localhost:8001
/services/cache/localhost:8002
/services/cache/localhost:8003
```

键名可以包含服务名和节点地址，具体格式由系统约定。

## 7. “节点注册自己”和“监听节点变化”是什么意思

### 7.1 注册自己

缓存节点启动后，把自己的 gRPC 地址写入 etcd：

```text
节点 A 启动
    ↓
写入 /services/cache/localhost:8001
    ↓
其他服务可以发现节点 A
```

实际系统通常会给这个注册信息绑定 lease，并定期发送 keepalive 心跳：

```text
节点在线 → 持续续租
节点宕机 → lease 过期
         → etcd 删除注册信息
         → 监听者收到节点下线事件
```

这样就不需要节点崩溃时自己执行注销。

### 7.2 监听变化

客户端或其他需要路由的组件可以 watch 某个前缀：

```text
watch /services/cache/
```

当节点加入或离开时，etcd 推送事件：

```text
PUT    /services/cache/localhost:8004
DELETE /services/cache/localhost:8002
```

监听者收到事件后，更新自己的节点列表和一致性哈希环：

```text
新增节点 → 加入节点列表和哈希环
删除节点 → 从节点列表和哈希环移除
```

“每个节点既注册自己，也监听其他节点变化”是一种常见的分布式系统模式，但并不意味着所有实现都必须这样做。实际部署中，通常由负责路由的客户端或网关监听节点列表，也可以由缓存节点自身监听，取决于系统的路由和同步设计。

## 8. etcd、gRPC 和一致性哈希的完整流程

### 8.1 读取流程

```text
缓存节点启动
    ↓
向 etcd 注册自己的 gRPC 地址
    ↓
客户端 SDK watch etcd，得到节点列表
    ↓
客户端 SDK 更新一致性哈希环
    ↓
根据 key 选择节点 B
    ↓
通过 gRPC 请求节点 B
    ↓
节点 B 的 Service 实现调用 CacheGroup
    ↓
查询本地 LRU，未命中时调用本地 DataGetter
    ↓
返回 GetResponse
```

### 8.2 写入和失效流程

一种常见设计是：

```text
Set：
客户端 SDK → 主节点 Set
客户端 SDK → 其他节点 Invalidate

Delete：
客户端 SDK → 所有节点 Delete
```

这说明“节点间的一致性同步”可以通过 gRPC 完成，但实际发起广播的对象不一定是某个缓存节点，也可能是客户端或网关。

## 9. 常见的路由模式

分布式缓存中常见的路由位置有两种：

### 9.1 客户端路由

客户端或网关通过服务发现得到节点列表，使用一致性哈希选择目标节点，再直接调用目标节点的 gRPC Server：

```text
客户端 / 网关
    → etcd 服务发现
    → 一致性哈希选择节点 B
    → gRPC 请求节点 B
    → 节点 B 的本地缓存
```

### 9.2 节点内部路由

请求先到节点 A，A 在本地缓存未命中后，通过节点选择器找到节点 B，再作为 gRPC Client 请求 B：

```text
请求到达节点 A
    → A 本地缓存 miss
    → 节点选择器选择节点 B
    → A 作为 gRPC Client 请求 B
    → B 的 gRPC Server 查询本地缓存
```

两种模式都可以使用 gRPC，区别在于一致性哈希和远程请求由客户端负责，还是由缓存节点内部负责。

## 10. 核心职责边界

```text
etcd              负责节点发现
一致性哈希        负责节点选择
gRPC               负责 RPC 通信
gRPC Service       负责请求适配
CacheGroup         负责缓存语义
LRU Cache          负责本地存储和淘汰
DataGetter         负责回源
```

