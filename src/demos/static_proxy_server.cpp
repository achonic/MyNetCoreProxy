// static_proxy_server.cpp
#include "net/server.h"
#include "net/event_loop.h"
#include "net/inet_address.h"
#include "app/http_proxy_handler.h"
#include <iostream>
#include <unordered_map>
#include <memory>

int main(int argc, char* argv[]) {
    // Port configurations
    uint16_t listen_port = 8080;
    
    LOG_DEBUG << "Starting Pure C++ Static HTTP Proxy Server..." << std::endl;

    // 1. Create the main EventLoop
    net::EventLoop main_loop;

    // 2. Define the static routing table
    // Maps HTTP Request Path -> Backend IP:Port
    std::unordered_map<std::string, std::string> routing_rules = {
        {"/api/v1/user", "127.0.0.1:8081"},
        {"/api/v1/order", "127.0.0.1:8082"},
        {"/", "127.0.0.1:8083"} // Default fallback route
    };

    LOG_DEBUG << "=== Proxy Routing Table ===" << std::endl;
    for (const auto& rule : routing_rules) {
        LOG_DEBUG << "ROUTE: " << rule.first << " -> " << rule.second << std::endl;
    }
    LOG_DEBUG << "===========================" << std::endl;

    // 3. Create listen address and Server instance
    net::InetAddress listen_addr("0.0.0.0", listen_port);
    net::Server proxy_server(&main_loop, listen_addr);

    // 4. Set the HandlerFactory to create an HttpProxyHandler for each new connection.
    proxy_server.setHandlerFactory([&routing_rules](net::TcpConnection* /*conn*/) {
        return std::make_unique<app::HttpProxyHandler>(routing_rules);
    });

    // 5. Start the proxy server (which spins up the thread pool and starts listening)
    LOG_DEBUG << "Proxy Server listening on port " << listen_port << "..." << std::endl;
    proxy_server.start();

    // 6. Enter the main event loop
    LOG_DEBUG << "Press Ctrl+C to stop the proxy server." << std::endl;
    main_loop.loop();

    LOG_DEBUG << "Proxy Server stopped." << std::endl;
    return 0;
}
