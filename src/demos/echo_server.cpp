// main.cpp
#include "net/server.h"
#include "net/event_loop.h"
#include "net/inet_address.h"
#include <cstdlib>
#include <iostream>

int main() {
    LOG_DEBUG << "Starting simple echo Server..." << std::endl;

    // 1. 创建主线程的 EventLoop
    net::EventLoop main_loop;

    // 2. 创建监听地址
    net::InetAddress listen_addr("127.0.0.1", 8080);

    // 3. 创建 Server 实例
    net::Server server(&main_loop, listen_addr);

    // 4. 设置连接回调
    server.setConnectionCallback([](net::TcpConnection* conn) {
        if (conn->connected()) {
            LOG_DEBUG << "New connection [" << conn->name() 
                      << "] from " << conn->peerAddress().toIpPort() << std::endl;
        } else {
            LOG_DEBUG << "Connection [" << conn->name() << "] closed" << std::endl;
        }
    });

    // 5. 设置消息回调（Echo逻辑）
    server.setMessageCallback([](net::TcpConnection* conn,
                                 net::Buffer* buffer) {
        std::string msg(buffer->retrieveAllAsString());
        LOG_DEBUG << "Received message from [" << conn->name() 
                  << "] - echoing back: " << msg;
        conn->send(msg);
    });

    // 6. 启动服务器
    LOG_DEBUG << "Starting echo server on port 8080..." << std::endl;
    server.start();

    // 7. 启动事件循环
    LOG_DEBUG << "Server is running. Waiting for connections..." << std::endl;
    LOG_DEBUG << "Press Ctrl+C to stop." << std::endl;

    if (const char* exit_after = std::getenv("ECHO_SERVER_EXIT_AFTER_MS")) {
        int delay_ms = std::atoi(exit_after);
        if (delay_ms > 0) {
            main_loop.runAfter(delay_ms / 1000.0, [&main_loop]() {
                main_loop.quit();
            });
        }
    }

    main_loop.loop();

    LOG_DEBUG << "Server stopped." << std::endl;
    return 0;
}