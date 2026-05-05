#pragma once
#include "net/inet_address.h"
#include <vector>
#include <mutex>
#include <set>

namespace app {

class LoadBalancer {
public:
    LoadBalancer();
    
    // 增加后端（初始默认为不健康，等待健康检查确认）
    void addBackend(const net::InetAddress& addr);
    void removeBackend(const net::InetAddress& addr);
    
    // 获取下一个健康的后端
    bool getNextBackend(net::InetAddress& out_addr);

    // 更新后端健康状态（由 HealthChecker 调用）
    void updateHealthStatus(const net::InetAddress& addr, bool healthy);

    // 获取所有需要检查的后端
    std::vector<net::InetAddress> getAllBackends();

private:
    // 所有配置的后端
    std::set<net::InetAddress> all_backends_set_;
    // 当前健康的后端列表（用于轮询）
    std::vector<net::InetAddress> healthy_backends_;
    
    size_t next_index_;
    std::mutex mutex_;
};

} // namespace app