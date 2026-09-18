# dcache

一个基于 C++17 的分布式缓存库。

当前项目已经完成本地缓存、一致性哈希和 Protobuf 协议的基础实现；节点发现、节点间通信和缓存同步还在开发中。

## 设计

缓存项由 `(group, key)` 共同确定：

```text
请求(group, key)
      │
      ▼
缓存组注册表
      │
      ▼
本地 LRU 缓存 ──命中──► 返回结果
      │
    未命中
      ▼
SingleFlight 合并并发请求
      │
      ▼
DataGetter 从数据源加载并写回缓存
```

未来接入分布式能力后，未命中请求会先通过一致性哈希选择负责节点，再通过 gRPC 请求远程节点：

```text
请求
  │
  ▼
一致性哈希选择节点
  │
  ▼
gRPC Client ── Protobuf ──► gRPC Server
                              │
                              ▼
                          本地缓存组
```

## 核心组件

| 组件 | 作用 |
| --- | --- |
| `LRUCache` | 保存当前进程的缓存数据，按容量淘汰最久未使用的条目 |
| `DCacheGroup` | 管理一个业务缓存组，负责本地读写和数据回源 |
| `SingleFlight` | 合并同一个 key 的并发回源请求，避免重复访问数据源 |
| `DCacheGroupRegistry` | 管理当前进程中的所有缓存组 |
| `ConsistentHashMap` | 根据 key 选择负责的缓存节点 |
| `dcache.proto` | 定义缓存节点之间的 `Get`、`Set`、`Delete`、`Invalidate` 协议 |

### `DCacheGroup` 的基本行为

- `get`：先查本地缓存，未命中时调用 `DataGetter` 并回填。
- `set`：只写当前节点的本地缓存。
- `deleteByKey`：只删除当前节点的缓存。
- `invalidateFromPeer`：处理其他节点发来的失效通知，只删除本地副本。

目前 `DCacheGroup` 还不会自动访问其他节点。

### `ConsistentHashMap` 的基本行为

一致性哈希只负责“选择节点”，不负责保存或读取缓存数据：

```text
key ──► 哈希环 ──► 节点名称
```

默认使用 CRC32 和虚拟节点，并提供节点增删、请求统计和负载调整能力。

## 当前状态

已完成：

- 本地 LRU 缓存
- 缓存组和数据回源
- SingleFlight 并发请求合并
- 进程内缓存组注册表
- 一致性哈希节点路由
- Protobuf 消息和 RPC 协议定义
- 核心组件单元测试

## TODO

- [ ] 接入 `PeerPicker`，把缓存组和一致性哈希连接起来。
- [ ] 实现 gRPC Server 和 Client，完成节点间远程读写。
- [ ] 接入 etcd，实现节点注册、心跳和 Watch。
- [ ] 将节点变化同步到本地节点表和哈希环。
- [ ] 明确 `Set`、`Delete`、`Invalidate` 的同步、重试和幂等规则。
- [ ] 完善协议中的命中状态、错误信息、超时和版本字段。
- [ ] 补充多节点、故障恢复、网络异常和并发压力测试。
- [ ] 完善监控指标、日志、健康检查和安全认证。
- [ ] 修复一致性哈希中的哈希碰撞处理问题。

## 目录

```text
src/core/       # LRU、缓存组、SingleFlight、一致性哈希
src/proto/      # Protobuf 协议
src/utils/      # CRC32 等工具
test/           # 单元测试
snippet/        # 实验代码
docs/           # 设计文档
```

## 相关文档

- [一致性哈希](docs/一致性哈希.md)
- [缓存组](docs/缓存组.md)
- [分布式缓存通信基础](docs/分布式缓存通信基础.md)

