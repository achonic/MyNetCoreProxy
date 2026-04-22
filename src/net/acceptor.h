#pragma once
#include "inet_address.h"
#include "utils/noncopyable.h"
#include <functional>
#include <iostream>
#include <memory>
namespace net {

class EventLoop;
class Channel;

class Acceptor : private utils::NonCopyable {
public:
  using NewConnectionCallback =
      std::function<void(int sockfd, const InetAddress &)>;

  Acceptor(EventLoop *loop, const InetAddress &listenaddr);
  ~Acceptor();

  void setNewConnectCallback(NewConnectionCallback cb) {
    new_connection_callback_ = std::move(cb);
  }
  void listen();

private:
  void handleRead();

  EventLoop *loop_;
  InetAddress listen_addr_;
  int listen_socketFd_; // 用于监听连接请求的 socket fd
  int idle_fd_;         // 空闲占位 fd，用于处理 EMFILE
  std::unique_ptr<Channel> listen_channel_;
  NewConnectionCallback new_connection_callback_;

  bool listening_;
};

} // namespace net