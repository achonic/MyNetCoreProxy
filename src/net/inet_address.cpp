#include "inet_address.h"
#include <arpa/inet.h> // for inet_ntoa, htons, inet_pton
#include <cstring>     // for memset

// InetAddress类的作用是做地址格式封装，适配socket库的风格

namespace net {

InetAddress::InetAddress(std::string ip, uint16_t port) {
    ::memset(&addr_, 0, sizeof(addr_));
    addr_.sin_family = AF_INET;
    addr_.sin_addr.s_addr = ::inet_addr(ip.c_str()); // 或者使用 inet_pton 更安全
    addr_.sin_port = ::htons(port);
}

InetAddress::InetAddress(const struct sockaddr_in& addr)
    : addr_(addr) {}

std::string InetAddress::toIp() const {
    char ip[INET_ADDRSTRLEN];
    ::inet_ntop(AF_INET, &addr_.sin_addr, ip, INET_ADDRSTRLEN);
    return std::string(ip);
}

std::string InetAddress::toIpPort() const {
    char ip[INET_ADDRSTRLEN];
    ::inet_ntop(AF_INET, &addr_.sin_addr, ip, INET_ADDRSTRLEN);
    uint16_t port = ::ntohs(addr_.sin_port);
    return std::string(ip) + ":" + std::to_string(port);
}

uint16_t InetAddress::toPort() const {
    return ::ntohs(addr_.sin_port);
}

}