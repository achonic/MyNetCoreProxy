# 从 UDS 混合代理到纯 C++ 静态代理的改造指南

本指南详细记录了如何将依赖 Go 控制面下发路由的 `HttpProxyHandler` 重构为完全独立运行的高性能纯 C++ 反向代理网关。

## 1. 架构变更概述

在以往的架构中，C++ 网络库（负责数据面通讯）和 Go 进程（负责控制面路由）通过 Unix Domain Sockets (UDS) 进程间通信交换数据。当 C++ 端接收到新的 HTTP 请求时，它会将 Host 和 Path 提取出来，发送给 Go 控制面。

由于 UDS IPC 和外部进程管理不仅增加了部署难度，也不利于我们测试 C++ 核心网络库的纯粹性能，我们此次实施了**解耦与静态化**：
彻底移除了 `ipc/UDSServer.h` 的依赖，不再向外部进程挂起并等待路由响应，而是将 HTTP 路径映射直接在 `HttpProxyHandler` 初始化时注入到内存当中（即静态路由表 `std::unordered_map`），实现**毫秒级内部直转**。

## 2. 核心模块修改清单

### 2.1 移除 UDS 依赖并引入路由表 
**文件：`src/app/http_proxy_handler.h`**
```diff
- #include "ipc/UDSServer.h"
+ // 移除了 UDS 头文件依赖

class HttpProxyHandler : public net::ConnectionHandler {
public:
-   explicit HttpProxyHandler(::UDSServer* uds_server);
+   explicit HttpProxyHandler(const std::unordered_map<std::string, std::string>& routing_table);

private:
-   UDSServer* uds_server_;
+   std::unordered_map<std::string, std::string> routing_table_;
    
-   void startForwarding(net::TcpConnection* conn, const std::string& backend_addr);
+   void startForwarding(net::TcpConnection* conn, const std::string& path);
};
```
在此修改中，原有的外部进程触发器指针被静态表替代。

### 2.2 本地直查路由与立即发起连接
**文件：`src/app/http_proxy_handler.cpp`**
原来在 `onMessage` (接收到 HTTP 报头时) 函数内：
```cpp
// 旧逻辑：打包事件并通过 UDS 发送给 Go
if (uds_server_) {
    NewConnectionEvent event;
    event.initial_data = host + "|" + path;
    uds_server_->sendNewConnectionEvent(event);
}
```
被直接替换成了瞬间在内存里全速查找与创建的新逻辑：
```cpp
// 新逻辑：直接本地查找路由表并转发
startForwarding(conn, path);
```

而在 `startForwarding` 内部实现了快速匹配机制：
```cpp
void HttpProxyHandler::startForwarding(net::TcpConnection* conn, const std::string& path) {
    std::string backend_addr = "";
    
    // 精确匹配路径查找
    auto it = routing_table_.find(path);
    if (it != routing_table_.end()) {
        backend_addr = it->second;
    } else {
        // 尝试 fallback (兜底路由)
        auto default_it = routing_table_.find("/");
        if (default_it != routing_table_.end()) {
            backend_addr = default_it->second;
        } else {
            // 没有命中任何规则，返回 404
            conn->send("HTTP/1.1 404 Not Found\r\n\r\n");
            conn->shutdown();
            return;
        }
    }
    
    // 发起异步的非阻塞的后端连接
    createBackendConnection(conn, backend_addr_obj, backend_addr);
}
```

### 2.3 打造独立的静态网关 Demo
**文件：`src/demos/static_proxy_server.cpp` (新增)**
此文件成为了这套架构最精简也最强大的使用说明书。
在里面我们手写了一套硬编码的哈希表作为路由配置：
```cpp
// 静态路由表定义
std::unordered_map<std::string, std::string> routing_rules = {
    {"/api/v1/user", "127.0.0.1:8081"},
    {"/api/v1/order", "127.0.0.1:8082"},
    {"/", "127.0.0.1:8083"} // Default fallback
};

// 注入 Server Handler
proxy_server.setHandlerFactory([&routing_rules](net::TcpConnection* /*conn*/) {
    return std::make_unique<app::HttpProxyHandler>(routing_rules);
});
```

## 3. 这套方案带来的直观优势

1. **去除了进程间(IPC)通信开销**：在真正的互联网高并发环境（如每秒 5 万个新连接），此前的方案必须进行 `5w次/秒x2` 的套接字跨内核态写入与唤醒操作。纯 C++ 的 `unordered_map::find` 操作是 `$O(1)$` 量级，基本可以视为 **0 损耗**。
2. **完整的状态机自闭环**：`MyNetCore` 现在自己全权掌管了“建立连接 -> 读取报头 -> 查找目标 -> 向后连接 -> 绑定双向数据阀门（Relay） -> 断开回收”这条完整的生命周期。
3. **极简运维**：编译产出的 `static_proxy_server` 是一个不需要携带任何运行时的单文件二进制程序。这就是 C++ 在云原生的最佳展现。
