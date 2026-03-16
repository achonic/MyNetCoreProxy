#pragma once

#include "acceptor.h"
#include "thread_pool.h"
#include "inet_address.h"
#include "tcp_connection.h"
#include "connection_handler.h"
#include <map>
#include <memory>
#include <atomic>
#include <mutex>

namespace net {

class EventLoop;

class Server {
public:
    // Handler factory: creates a handler for each new connection
    using HandlerFactory = std::function<std::unique_ptr<ConnectionHandler>(TcpConnection*)>;

    Server(EventLoop* loop, const InetAddress& listenAddr);
    ~Server();

    void start();

    // Handler factory setter
    void setHandlerFactory(HandlerFactory factory) {
        handlerFactory_ = std::move(factory);
    }

    // 回调设置（fallback when no handler is used）
    void setConnectionCallback(TcpConnection::ConnectionCallback cb) {
        connectionCallback_ = std::move(cb);
    }
    void setMessageCallback(TcpConnection::MessageCallback cb) {
        messageCallback_ = std::move(cb);
    }
    void setWriteCompleteCallback(TcpConnection::WriteCompleteCallback cb) {
        writeCompleteCallback_ = std::move(cb);
    }
    void setCloseCallback(TcpConnection::CloseCallback cb) {
        closeCallback_ = std::move(cb);
    }

    // 连接查找方法
    std::shared_ptr<TcpConnection> findConnectionByFd(int conn_fd);

private:
    void newConnection(int sockfd, const InetAddress& peerAddr);
    void removeConnection(const TcpConnection* conn);

    EventLoop* loop_;
    std::unique_ptr<Acceptor> acceptor_;

    const std::string hostport_;
    
    std::unique_ptr<threadPool> threadPool_;

    std::atomic<int> started_;

    // 连接管理
    std::map<std::string, std::shared_ptr<TcpConnection>> connections_;
    mutable std::mutex mutex_;

    // Handler factory
    HandlerFactory handlerFactory_;

    // 回调函数 (fallback)
    TcpConnection::ConnectionCallback connectionCallback_;
    TcpConnection::MessageCallback messageCallback_;
    TcpConnection::WriteCompleteCallback writeCompleteCallback_;
    TcpConnection::CloseCallback closeCallback_;
};

} 