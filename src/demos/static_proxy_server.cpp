// static_proxy_server.cpp
#include "net/server.h"
#include "net/event_loop.h"
#include "net/inet_address.h"
#include "app/load_balancer.h"
#include "app/load_balancer_handler.h"
#include "app/health_checker.h"
#include <iostream>
#include <memory>

int main(int argc, char* argv[]) {
    // Port configurations
    uint16_t listen_port = 8080;
    
    LOG_DEBUG << "Starting Pure C++ L4 Load Balancer Gateway..." << std::endl;

    // 1. Create the main EventLoop
    net::EventLoop main_loop;

    // 2. Define the load balancer and backend servers
    auto load_balancer = std::make_shared<app::LoadBalancer>();
    load_balancer->addBackend(net::InetAddress("127.0.0.1", 8001));
    load_balancer->addBackend(net::InetAddress("127.0.0.1", 8002));
    load_balancer->addBackend(net::InetAddress("127.0.0.1", 8003));

    // 3. Start Health Checker
    app::HealthChecker health_checker(&main_loop, load_balancer);
    health_checker.start(3.0); // Every 3 seconds

    LOG_DEBUG << "=== Load Balancer Backends (Health Check Active) ===" << std::endl;
    LOG_DEBUG << " - 127.0.0.1:8001" << std::endl;
    LOG_DEBUG << " - 127.0.0.1:8002" << std::endl;
    LOG_DEBUG << " - 127.0.0.1:8003" << std::endl;
    LOG_DEBUG << "===========================" << std::endl;

    // 3. Create listen address and Server instance
    net::InetAddress listen_addr("0.0.0.0", listen_port);
    net::Server proxy_server(&main_loop, listen_addr);

    // 4. Set the HandlerFactory to create a LoadBalancerHandler for each new connection.
    proxy_server.setHandlerFactory([load_balancer](net::TcpConnection* /*conn*/) {
        return std::make_unique<app::LoadBalancerHandler>(load_balancer);
    });

    // 5. Start the proxy server (which spins up the thread pool and starts listening)
    LOG_DEBUG << "Gateway listening on port " << listen_port << "..." << std::endl;
    proxy_server.start();

    // 6. Enter the main event loop
    LOG_DEBUG << "Press Ctrl+C to stop the gateway." << std::endl;
    main_loop.loop();

    LOG_DEBUG << "Gateway stopped." << std::endl;
    return 0;
}
