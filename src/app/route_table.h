#include "utils/noncopyable.h"
#include <iostream>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>

// 采用单例模式或作为 Server 的成员变量注入
namespace net {
class RouteTable {
public:
  static RouteTable &Instance() {
    static RouteTable instance;
    return instance;
  }

  // C++ EventLoop 高频调用（加读锁，允许多个 epoll 线程同时查）
  std::string getBackend(const std::string &host) {
    std::shared_lock<std::shared_mutex> lock(rw_mutex_);
    auto it = routes_.find(host);
    if (it != routes_.end()) {
      return it->second;
    }
    return ""; // 没找到路由
  }

  // Go 通过 UDS 异步推送时调用（加写锁，独占）
  void updateRoute(const std::string &host, const std::string &backend_addr) {
    std::unique_lock<std::shared_mutex> lock(rw_mutex_);
    routes_[host] = backend_addr;
    LOG_DEBUG << "[RouteTable] Updated: " << host << " -> " << backend_addr
              << std::endl;
  }

private:
  RouteTable() = default;
  std::unordered_map<std::string, std::string> routes_;
  std::shared_mutex rw_mutex_;
};

} // namespace net