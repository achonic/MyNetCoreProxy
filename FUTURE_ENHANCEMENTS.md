# MyNetCore：核心架构隐患与演进路线图 (Phase 2 Roadmap)

经过前一阶段的重构，我们已经成功实现了一个性能极高的纯 C++ 非阻塞异步反向代理雏形。但是，要让这个框架从“能在试验田跑通”蜕变为“能够抵御真实恶劣网络环境的**工业级完整项目**”，下面三个维度的问题是**最核心、最必须立刻改进的生命线**。

---

## ⭐️ 核心问题一：TCP 字节流边界识别（粘包/半包危机）- 最紧急！

**现状与隐患：**
目前 `HttpProxyHandler::onMessage` 在解析 HTTP 请求路径时，只要 `epoll` 唤醒了读事件，就会立刻对 `Buffer` 里的可用数据进行字符串转换和解析。但由于 TCP 是“无边界的字节流”：
- **半包灾难**：极有可能前一次 `read()` 只读到了 `GET /api/`，没有读全 HTTP Header。目前的逻辑会直接返回等待下次数据，但没有一套完善的机制标记当前解析的“游标（Cursor）”，导致状态机错乱甚至遭遇慢速攻击。
- **劫持与污染（粘包）**：后端建立连接后，我们无脑把前端堆积的数据全部塞给 `modifyRequestForBackend` 修改 `Host`。如果堆积数据里混杂着完整的 HTTP Body 二进制数据（比如上传的图片内恰好有 "Host: " 字节序列），或者前后两个请求黏在了一起，数据包将直接被损坏！

**改进方案：**
- **严厉的界限检测**：在进入 Header 解析前，必须在缓冲区中 `findCRLF()` (寻找 `\r\n\r\n`)。只有找到了，才证明收到了一个**完整合法的 HTTP 请求头部**。
- **协议状态隔离**：将 `HttpProxyHandler` 的解析状态严格分为：`EXPECT_HEADERS` -> `EXPECT_BODY` -> `RELAY_MODE`。在替换 Host 标头时，只精确操作 Header 那一部分的子串，剩余的 Body 字节必须一字节不落地原样 `send()` 到后端。

---

## ⭐️ 核心问题二：连接生命周期的超时管理 (Timeout Control) - 最必须！

**现状与隐患：**
- **黑洞连接**：目前我们在 `createBackendConnection` 中发起了非阻塞 `connect()`，如果后端那个 IP 实际上是一个路由黑洞（比如防火墙正在 Drop 包），这个连接过程会永远卡在 `SYN_SENT` 或者被挂起。前端的这笔客户端连接将永远得不到关闭，一直占用服务器的文件描述符（fd）和内存。
- **僵尸资源泄漏**：如果一个恶意客户端建立连接后，什么数据都不发，目前的系统会永远把它供养在内存树（`epoll` 例程）中。这也就是著名的并发耗尽攻击。

**改进方案：**
- **引入定时器轮 (Timing Wheel) 或时间堆**：为 `EventLoop` 增添定时功能模块 `TimerQueue`。
- **懒惰关闭机制 (Idle Timeout)**：前端连接在建立后如果 XX 秒内未发来完整的 HTTP 请求、或者后端 `connect()` 超过 XX 秒未完成，必须由定时器强制触发 `TcpConnection::forceClose()` 杀掉连接，回收资源。
- 作为代理服务器，必须加入 HTTP 的 `Keep-Alive` 协商和自动降级断开的逻辑。

---

## ⭐️ 核心问题三：双向连接回收的“联动雪崩” (Tear-down synchronization) - 最棘手！

**现状与隐患：**
你现在正在代码中看到并操作着：
`backend_conn_->setMessageCallback(...)` 和 `backend_conn_->setConnectionCallback(...)`。
当任何一端（比如客户端 Chrome 强行点击了断开，或者后端 Python 服务器突然崩溃），会引发一个剧烈的连锁反应。你目前在 `close_callback` 里只进行了初步的联动关闭。但是在深度异常的网络波动中：
如果前端触发了 `shutdown`，但这同时恰好后端的 `epoll_wait` 有数据被抛上来触发了 `handleRead` 进而调用 `frontend_conn->send()`，此时如果 `frontend_conn` 的对象生命周期刚刚被析构掉（由于你使用了 shared_ptr / weak_ptr 等），程序就会因为**悬挂指针（Segmentation Fault）或者写入崩溃的管节而引发服务器闪退！**

**改进方案：**
- **引入 std::weak_ptr 守护**：在处理跨连接的联动闭环中（Frontend 对 Backend，Backend 又抓着 Frontend），不能互持。必须用 `weak_ptr`。在执行对另一个连接的操作前，必须 `lock()` 检查存活状态。
- **半关闭状态 (Half-close) 识别**：合理利用 `epoll` 的 `EPOLLRDHUP`。允许客户端关闭写端（不再发请求），但还能接着接收后端的残余响应数据，只有等双端都完全榨干后才把对象彻底销毁。

---

## 🏁 结项前夜的终极步骤 (Next Actions)

如果要让这件作品真正成型：
1. 先修改 `HttpProxyHandler`，引入 `\r\n\r\n` 探测循环。
2. 配置基于 `steady_clock` 或 `timerfd` 的初级定时器，斩杀所有超过 10 秒连不上的后端。
3. Review 一下 `TcpConnection` 的析构函数和 `std::shared_ptr` 管理链条。

只要这三步做完了，这就是一个不可挑剔的现代 C++ 网络架构项目。
