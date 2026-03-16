#pragma once
#include "utils/noncopyable.h"
#include "buffer.h"
#include "inet_address.h"
#include "event_loop.h"
#include "channel.h"
#include <fcntl.h>
#include <string>
#include <functional>
#include <atomic>
#include <memory>

namespace net{

class ConnectionHandler;

class TcpConnection : utils::NonCopyable{
public:
    TcpConnection(EventLoop* loop,
                 const std::string& name, 
                 int connFd, 
                 const InetAddress& peer_addr = InetAddress());
    ~TcpConnection();

    using ConnectionCallback = std::function<void(TcpConnection*)>;
    using MessageCallback = std::function<void(TcpConnection*, Buffer*)>;
    using WriteCompleteCallback = std::function<void(TcpConnection*)>;
    using CloseCallback = std::function<void(TcpConnection*)>;

    void setConnectionCallback(const ConnectionCallback& cb);
    void setMessageCallback(const MessageCallback& cb);
    void setWriteCompleteCallback(const WriteCompleteCallback& cb);
    void setCloseCallback(const CloseCallback& cb);

    void connectEstablished();
    void connectEstablishedAsClient();
    void connectDestroyed();

    void send(const std::string& message);
    void shutdown();

    EventLoop* getLoop() const { return loop_; }

    int get_fd() const {return conn_fd_; }
    const std::string& name() const { return name_; }
    const InetAddress& peerAddress() const {return peer_addr_; }
    bool connected() const { return state_ == kConnected; }
    bool disconnected() const { return state_ == kDisConnected; }

    // Handler support (Strategy pattern)
    void setHandler(std::unique_ptr<ConnectionHandler> handler);
    ConnectionHandler* getHandler() const { return handler_.get(); }

    // Buffer accessors for handler use
    Buffer& inputBuffer() { return input_buffer_; }
    Buffer& outputBuffer() { return output_buffer_; }

private:
    void handleRead();
    void handleWrite();
    void handleClose();
    void handleError();

    void sendInLoop(const std::string& message);
    void shutdownInLoop();

    enum State{
        kDisConnected,
        kConnected,
        kConnecting,
        kDisConnecting
    };
    const InetAddress& peer_addr_;

    EventLoop* loop_;
    const std::string name_;
    int conn_fd_;
    std::unique_ptr<Channel> channel_;
    Buffer input_buffer_;
    Buffer output_buffer_;

    // 线程安全
    std::atomic<State> state_;

    // Handler (protocol/business logic)
    std::unique_ptr<ConnectionHandler> handler_;

    // 回调函数 (fallback when no handler is set)
    ConnectionCallback connection_callback_;
    MessageCallback message_callback_;
    WriteCompleteCallback write_complete_callback_;
    CloseCallback close_callback_;
};

}