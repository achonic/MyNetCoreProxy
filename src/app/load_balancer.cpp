#include "load_balancer.h"
#include <algorithm>
#include <iostream>

namespace app {

LoadBalancer::LoadBalancer() : next_index_(0) {}

void LoadBalancer::addBackend(const net::InetAddress &addr) {
  std::lock_guard<std::mutex> lock(mutex_);
  all_backends_set_.insert(addr);
  // 初始时不加入 healthy_backends_，等待第一次健康检查通过
}

void LoadBalancer::removeBackend(const net::InetAddress &addr) {
  std::lock_guard<std::mutex> lock(mutex_);
  all_backends_set_.erase(addr);
  healthy_backends_.erase(
      std::remove(healthy_backends_.begin(), healthy_backends_.end(), addr),
      healthy_backends_.end());
  if (next_index_ >= healthy_backends_.size() && !healthy_backends_.empty()) {
    next_index_ = 0;
  }
}

bool LoadBalancer::getNextBackend(net::InetAddress &out_addr) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (healthy_backends_.empty()) {
    return false;
  }

  out_addr = healthy_backends_[next_index_];
  next_index_ = (next_index_ + 1) % healthy_backends_.size();
  return true;
}

void LoadBalancer::updateHealthStatus(const net::InetAddress &addr,
                                      bool healthy) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = std::find(healthy_backends_.begin(), healthy_backends_.end(), addr);

  if (healthy) {
    if (it == healthy_backends_.end()) {
      healthy_backends_.push_back(addr);
      std::cout << "[LoadBalancer] Backend UP: " << addr.toIpPort()
                << std::endl;
    }
  } else {
    if (it != healthy_backends_.end()) {
      healthy_backends_.erase(it);
      std::cout << "[LoadBalancer] Backend DOWN: " << addr.toIpPort()
                << std::endl;
      if (next_index_ >= healthy_backends_.size()) {
        next_index_ = 0;
      }
    }
  }
}

std::vector<net::InetAddress> LoadBalancer::getAllBackends() {
  std::lock_guard<std::mutex> lock(mutex_);
  return std::vector<net::InetAddress>(all_backends_set_.begin(),
                                       all_backends_set_.end());
}

} // namespace app