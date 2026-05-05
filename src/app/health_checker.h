#pragma once
#include "net/event_loop.h"
#include "load_balancer.h"
#include <memory>

namespace app {

class HealthChecker {
public:
    HealthChecker(net::EventLoop* loop, std::shared_ptr<LoadBalancer> lb);
    
    void start(double interval_seconds = 3.0);

private:
    void checkBackends();
    void probe(const net::InetAddress& addr);

    net::EventLoop* loop_;
    std::shared_ptr<LoadBalancer> load_balancer_;
};

} // namespace app