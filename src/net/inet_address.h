#pragma once

#include <netinet/in.h> 
#include <string>


// 封装 sockaddr_in，方便地址和端口操作。
namespace net {

class InetAddress {
public:
    // 两种构造方式
    explicit InetAddress(std::string ip = "127.0.0.1", uint16_t port = 0);
    explicit InetAddress(const struct sockaddr_in& addr);

    std::string toIp() const;
    std::string toIpPort() const;
    uint16_t toPort() const;

    bool operator==(const InetAddress& other) const {
        return toIpPort() == other.toIpPort();
    }

    bool operator<(const InetAddress& other) const {
        return toIpPort() < other.toIpPort();
    }

    const struct sockaddr_in* getSockAddrInet() const { return &addr_; }
    void setSockAddrInet(const struct sockaddr_in& addr) { addr_ = addr; }

private:
    struct sockaddr_in addr_;
};

} // namespace net
