#include <iostream>
#include <string>
#include <functional>
#include <atomic>
#include <memory>
#include <vector>

namespace net {
class Buffer {
    std::vector<char> buffer_;
    size_t readerIndex_;
    size_t writerIndex_;
};
class InetAddress {
    struct sockaddr_in addr_;
};
class EventLoop;
class Channel;
class ConnectionHandler;
class TcpConnection {
    const InetAddress& peer_addr_;
    EventLoop* loop_;
    const std::string name_;
    int conn_fd_;
    std::unique_ptr<Channel> channel_;
    Buffer input_buffer_;
    Buffer output_buffer_;
    std::atomic<int> state_;
    std::unique_ptr<ConnectionHandler> handler_;
    std::function<void(TcpConnection*)> connection_callback_;
    std::function<void(TcpConnection*, Buffer*)> message_callback_;
    std::function<void(TcpConnection*)> write_complete_callback_;
    std::function<void(TcpConnection*)> high_water_mark_callback_;
    std::function<void(TcpConnection*)> close_callback_;
    size_t high_water_mark_;
};
}
int main() {
    std::cout << sizeof(net::TcpConnection) << std::endl;
    return 0;
}
