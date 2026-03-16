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
    //       LOG_DEBUG << "[Acceptor] setNewConnectCallback called" <<
    //       std::endl;
    new_conn_callback_ = std::move(cb);
  }
  void listen();

private:
  void handleRead();

  EventLoop *loop_;
  InetAddress listen_addr_;
  int accept_socketFd_; // 用于 accept 的 socket fd
  std::unique_ptr<Channel> accept_channel_;
  NewConnectionCallback new_conn_callback_;

  bool listening_;
};

} // namespace net