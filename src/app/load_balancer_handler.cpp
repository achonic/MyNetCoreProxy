#include "load_balancer_handler.h"
#include "net/buffer.h"
#include "net/event_loop.h"
#include <sys/socket.h>
#include <fcntl.h>
#include <iostream>
#include <unistd.h>
#include <cstring>

namespace app {

LoadBalancerHandler::LoadBalancerHandler(std::shared_ptr<LoadBalancer> lb)
    : load_balancer_(lb), backend_connected_(false), first_byte_received_(false) {}

LoadBalancerHandler::~LoadBalancerHandler() {
    // Timer 自动清理逻辑由 EventLoop/TimerQueue 保证
}

void LoadBalancerHandler::onConnection(net::TcpConnection *conn) {
    if (conn->connected()) {
        // 1. 设置首包超时定时器（5秒）
        initial_timer_id_ = conn->getLoop()->runAfter(5.0, [this, conn]() {
            onInitialTimeout(conn);
        });

        // 2. 选择后端
        net::InetAddress backend_addr;
        if (!load_balancer_->getNextBackend(backend_addr)) {
            LOG_DEBUG << "[" << conn->name() << "] No healthy backend available." << std::endl;
            conn->shutdown();
            return;
        }
        
        LOG_DEBUG << "[" << conn->name() << "] Selected backend: " << backend_addr.toIpPort() << std::endl;
        
        conn->getLoop()->runInLoop([this, conn, backend_addr]() {
            createBackendConnection(conn, backend_addr);
        });
    }
}

void LoadBalancerHandler::onMessage(net::TcpConnection *conn, net::Buffer *buffer) {
    // 收到数据，取消首包超时定时器
    if (!first_byte_received_) {
        conn->getLoop()->cancel(initial_timer_id_);
        first_byte_received_ = true;
        
        // 启动空闲超时定时器（60秒）
        idle_timer_id_ = conn->getLoop()->runAfter(60.0, [this, conn]() {
            onIdleTimeout(conn);
        });
    } else {
        // 刷新空闲定时器
        resetIdleTimer(conn);
    }

    if (backend_conn_ && backend_connected_) {
        std::string data = buffer->retrieveAllAsString();
        backend_conn_->send(data);
        LOG_DEBUG << "[" << conn->name() << "] Forwarded " << data.length() << " bytes to backend." << std::endl;
    }
}

void LoadBalancerHandler::onClose(net::TcpConnection *conn) {
    if (!first_byte_received_) {
        conn->getLoop()->cancel(initial_timer_id_);
    } else {
        conn->getLoop()->cancel(idle_timer_id_);
    }

    if (backend_conn_) {
        backend_conn_->shutdown();
        backend_conn_.reset();
    }
}

void LoadBalancerHandler::onInitialTimeout(net::TcpConnection *conn) {
    if (!first_byte_received_) {
        LOG_DEBUG << "[" << conn->name() << "] Initial data timeout (Slowloris protection). Closing." << std::endl;
        conn->shutdown();
    }
}

void LoadBalancerHandler::onIdleTimeout(net::TcpConnection *conn) {
    LOG_DEBUG << "[" << conn->name() << "] Idle timeout (60s). Closing." << std::endl;
    conn->shutdown();
}

void LoadBalancerHandler::resetIdleTimer(net::TcpConnection *conn) {
    conn->getLoop()->cancel(idle_timer_id_);
    idle_timer_id_ = conn->getLoop()->runAfter(60.0, [this, conn]() {
        onIdleTimeout(conn);
    });
}

void LoadBalancerHandler::createBackendConnection(net::TcpConnection *frontend_conn, const net::InetAddress &backend_addr) {
    int backend_sock = ::socket(AF_INET, SOCK_STREAM, 0);
    if (backend_sock < 0) {
        frontend_conn->shutdown();
        return;
    }

    int flags = ::fcntl(backend_sock, F_GETFL, 0);
    ::fcntl(backend_sock, F_SETFL, flags | O_NONBLOCK);

    const struct sockaddr *addr_ptr = reinterpret_cast<const sockaddr *>(backend_addr.getSockAddrInet());
    socklen_t addr_len = sizeof(struct sockaddr_in);

    int ret = ::connect(backend_sock, addr_ptr, addr_len);

    if (ret < 0 && errno != EINPROGRESS) {
        ::close(backend_sock);
        frontend_conn->shutdown();
        return;
    }

    std::string backend_name = frontend_conn->name() + "_backend";
    backend_conn_ = std::make_shared<net::TcpConnection>(
        frontend_conn->getLoop(), backend_name, backend_sock, backend_addr);

    setupBidirectionalRelay(frontend_conn);
    backend_conn_->connectEstablishedAsClient();
}

void LoadBalancerHandler::setupBidirectionalRelay(net::TcpConnection *frontend_conn) {
    // ... (背压逻辑保持不变，但增加响应时的 Idle 定时器刷新)
    backend_conn_->setMessageCallback(
        [this, frontend_conn](net::TcpConnection *backend_conn, net::Buffer *buffer) {
            resetIdleTimer(frontend_conn); // 收到后端响应也算活跃
            if (frontend_conn->connected()) {
                std::string data = buffer->retrieveAllAsString();
                frontend_conn->send(data);
            } else {
                buffer->retrieveAll();
            }
        });

    // 之前已实现的背压、连接建立逻辑 (此处简化合并)
    backend_conn_->setHighWaterMarkCallback([frontend_conn](net::TcpConnection*){ frontend_conn->stopRead(); }, 64*1024*1024);
    backend_conn_->setWriteCompleteCallback([frontend_conn](net::TcpConnection*){ frontend_conn->startRead(); });
    
    std::weak_ptr<net::TcpConnection> weak_backend = backend_conn_;
    frontend_conn->setHighWaterMarkCallback([weak_backend](net::TcpConnection*){ 
        if (auto b = weak_backend.lock()) b->stopRead(); 
    }, 64*1024*1024);
    frontend_conn->setWriteCompleteCallback([weak_backend](net::TcpConnection*){ 
        if (auto b = weak_backend.lock()) b->startRead(); 
    });

    backend_conn_->setConnectionCallback(
        [this, frontend_conn](net::TcpConnection *backend_conn) {
            if (backend_conn->connected()) {
                backend_connected_ = true;
                if (frontend_conn->inputBuffer().readableBytes() > 0) {
                    backend_conn->send(frontend_conn->inputBuffer().retrieveAllAsString());
                }
            } else {
                frontend_conn->shutdown();
            }
        });
        
    backend_conn_->setCloseCallback([frontend_conn](net::TcpConnection*){ frontend_conn->shutdown(); });
}

} // namespace app