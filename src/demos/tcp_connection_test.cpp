#include "net/tcp_connection.h"
#include "net/event_loop.h"
#include "net/event_loop_thread.h"
#include <iostream>
#include <thread>
#include <unistd.h> // for socketpair, close
#include <sys/socket.h>
#include <cstring>
int main() {
    net::EventLoopThread server_thread; // 创建一个 EventLoopThread
    net::EventLoop* server_loop = server_thread.startLoop(); // 获取其 EventLoop

    // 使用 socketpair 创建一对连接的 socket
    int sockets[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) < 0) {
        perror("socketpair");
        return -1;
    }

    int server_fd = sockets[0];
    int client_fd = sockets[1];

    // 创建 TcpConnection
    auto conn = std::make_unique<net::TcpConnection>(server_loop, "test_conn", server_fd);

    // 设置回调
    conn->setConnectionCallback([](net::TcpConnection* conn) {
        LOG_DEBUG << "Connection [" << conn->name() << "] established." << std::endl;
    });

    conn->setMessageCallback([](net::TcpConnection* tcp_conn, net::Buffer* buf) {
        std::string msg = buf->retrieveAllAsString();
        LOG_DEBUG << "Received message: " << msg << std::endl;
        // Echo back
        tcp_conn->send(msg); // 测试 sendInLoop
        LOG_DEBUG << "Echoed message back." << std::endl;
    });

    conn->setCloseCallback([](net::TcpConnection* conn) {
        LOG_DEBUG << "Connection [" << conn->name() << "] closed." << std::endl;
    });

    // 启动连接
    server_loop->runInLoop([&conn]() {
        conn->connectEstablished();
    });

    // 等待 EventLoopThread 启动完成 (可能需要一点时间)
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // 从 client_fd 发送数据，模拟客户端行为
    const char* test_msg = "Hello, TcpConnection!\n";
    write(client_fd, test_msg, strlen(test_msg));

    // 等待 Echo
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // --- 在 client_fd 读取数据 ---
    char buffer[1024];
    ssize_t n = read(client_fd, buffer, sizeof(buffer) - 1); // 从 client_fd 读取
    if (n > 0) {
        buffer[n] = '\0'; // 添加字符串结束符
        LOG_DEBUG << "Client received: " << buffer; // 确认收到的消息
    } else {
        LOG_DEBUG << "Client read failed or no data received." << std::endl;
        if (n < 0) {
            perror("read");
        }
    }

    // 测试跨线程 send
    std::thread sender_thread([&conn]() {
        conn->getLoop()->runInLoop([&conn]() { // 确保在 EventLoop 线程中操作 conn
            conn->send("Message from another thread.\n");
        });
    });
    sender_thread.join();

    // 等待处理
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    n = read(client_fd, buffer, sizeof(buffer) - 1); // 从 client_fd 读取
    if (n > 0) {
        buffer[n] = '\0'; // 添加字符串结束符
        LOG_DEBUG << "Client received: " << buffer; // 确认收到的消息
    } else {
        LOG_DEBUG << "Client read failed or no data received." << std::endl;
        if (n < 0) {
            perror("read");
        }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
     // 关闭 client_fd，触发 handleClose
    close(client_fd);
    // 等待关闭处理
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    // 这里 EventLoopThread 会等待，因为 TcpConnection 可能还在 EventLoop 中
    // 你需要在 handleClose 后从 Server 管理的容器中移除 TcpConnection 并删除它
    // 否则 EventLoopThread::join() 会卡住
    // 或者直接让 main 结束，强制线程结束 (不推荐用于生产)

    LOG_DEBUG << "Test finished." << std::endl;

    // 注意：这里没有优雅地停止 EventLoopThread 和清理 TcpConnection
    // 在实际 Server 中，Server 会管理 TcpConnection 的生命周期
    return 0; // 程序结束，线程会被强制结束
}