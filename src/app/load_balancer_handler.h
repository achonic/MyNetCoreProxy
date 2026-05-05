#pragma once
#include "net/connection_handler.h"
#include "net/tcp_connection.h"
#include "net/inet_address.h"
#include "load_balancer.h"
#include "net/timer_id.h"
#include <memory>

namespace app {

class LoadBalancerHandler : public net::ConnectionHandler {
public:
    explicit LoadBalancerHandler(std::shared_ptr<LoadBalancer> lb);
    ~LoadBalancerHandler() override;

    void onMessage(net::TcpConnection *conn, net::Buffer *buffer) override;
    void onConnection(net::TcpConnection *conn) override;
    void onClose(net::TcpConnection *conn) override;

private:
    void createBackendConnection(net::TcpConnection *frontend_conn, const net::InetAddress &backend_addr);
    void setupBidirectionalRelay(net::TcpConnection *frontend_conn);
    
    // 超时处理回调
    void onInitialTimeout(net::TcpConnection *conn);
    void onIdleTimeout(net::TcpConnection *conn);
    void resetIdleTimer(net::TcpConnection *conn);

    std::shared_ptr<LoadBalancer> load_balancer_;
    std::shared_ptr<net::TcpConnection> backend_conn_;
    bool backend_connected_;
    
    // 定时器 ID
    net::TimerId initial_timer_id_;
    net::TimerId idle_timer_id_;
    bool first_byte_received_;
};

} // namespace app