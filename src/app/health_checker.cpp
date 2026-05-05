#include "health_checker.h"
#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

namespace app {

HealthChecker::HealthChecker(net::EventLoop* loop, std::shared_ptr<LoadBalancer> lb)
    : loop_(loop), load_balancer_(lb) {}

void HealthChecker::start(double interval_seconds) {
    loop_->runEvery(interval_seconds, [this]() {
        checkBackends();
    });
}

void HealthChecker::checkBackends() {
    auto backends = load_balancer_->getAllBackends();
    for (const auto& addr : backends) {
        probe(addr);
    }
}

void HealthChecker::probe(const net::InetAddress& addr) {
    // 简单的 TCP 探测：尝试非阻塞 connect
    int sockfd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (sockfd < 0) return;

    int ret = ::connect(sockfd, reinterpret_cast<const sockaddr*>(addr.getSockAddrInet()), sizeof(struct sockaddr_in));
    
    if (ret == 0) {
        // 瞬间连上了（通常是本机或极快网络）
        load_balancer_->updateHealthStatus(addr, true);
        ::close(sockfd);
    } else {
        if (errno == EINPROGRESS) {
            // 连接正在进行中，使用一个临时的 Channel 监听可写事件
            auto channel = std::make_shared<net::Channel>(loop_, sockfd);
            
            // 设置一个探测超时定时器（如 1 秒）
            auto timerId = loop_->runAfter(1.0, [this, channel, addr, sockfd]() {
                if (channel->isWriting()) {
                    // 超时未连上
                    load_balancer_->updateHealthStatus(addr, false);
                    channel->disableAll();
                    channel->remove();
                    ::close(sockfd);
                }
            });

            channel->setWriteCallback([this, channel, addr, sockfd, timerId]() {
                // 可写说明 connect 完成（无论成功失败，需通过 getsockopt 检查）
                loop_->cancel(timerId);
                
                int error = 0;
                socklen_t len = sizeof(error);
                if (::getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &error, &len) < 0 || error != 0) {
                    load_balancer_->updateHealthStatus(addr, false);
                } else {
                    load_balancer_->updateHealthStatus(addr, true);
                }
                
                channel->disableAll();
                channel->remove();
                ::close(sockfd);
            });
            channel->enableWriting();
        } else {
            // 直接报错
            load_balancer_->updateHealthStatus(addr, false);
            ::close(sockfd);
        }
    }
}

} // namespace app