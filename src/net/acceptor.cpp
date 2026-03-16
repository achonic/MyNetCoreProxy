#include "acceptor.h"
#include "channel.h"
#include "event_loop.h"
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
/*
    Accpetor类描述（主从Reactor结构里的主Reactor，负责监听新连接）

    Accpetor类，在项目中由Server实例化，Server设置和控制Acceptor的操作
    （server独占Accpetor）

    1、构造函数Acceptor::Acceptor(EventLoop* loop, const InetAddress&
   listen_addr); 传入 <主线程eventloop> 和 <监听的地址>
    创建sokcet套件字即连接的接口，然后注册loop和accept_socketFd_到Channel中
    Acceptor设置Channel的监听到Channel读事件（即socket连接）回调函数，
    异步触发handleRead()函数
    2、析构函数
    调用remove，关闭fd
    3、listen()
    被Server.start()主线程调用，做bind和listen操作，并开启epoll监听
    （即激活<异步触发handleRead()>的回调函数），此时若监听到事件，就触发handleRead()。
    4、handleRead()
    handleRead里调用socket的accept4，获取客户端的socketfd和地址peer_addr。
    然后把socketfd和peer_addr通过回调，传入Server的newConnection中。
    交由Server为该客户端分配子线程，用来处理I/O事物。


    至此，Acceptor对一条新连接需要做的任务就完成了，Acceptor的任务就
    只需要监听并且接收新连接，然后把新连接的信息传给Server。


    Acceptor实例，在Eventloop里循环执行，Channel注册后由Epoll触发连接事件。
    新连接会唤醒Epoll_wait，然后，Epoll的事件的格式是Channel类。
    所以在Eventloop里可以看到，唤醒后，Epoll_event的ptr存了该事件Channel的地址
    然后获取地址，执行Epoll事件处理handleEvent
    handleEvent里触发的是Channel的读、写回调事件，
    由于注册的时候设置的是读，所以执行读回调，这个Channel是由Acceptor的实例化的，
    所以执行的是handleRead()。作用如上所述。
    这就是Acceptor实例在执行时的所有事件。


*/

namespace net {

Acceptor::Acceptor(EventLoop *loop, const InetAddress &listen_addr)
    : loop_(loop), listen_addr_(listen_addr), listening_(false) {
  // 设置流式，非阻塞TCP套接字
  accept_socketFd_ =
      ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, IPPROTO_TCP);
  accept_channel_ = std::make_unique<Channel>(loop_, accept_socketFd_);
  if (accept_socketFd_ < 0) {
    LOG_DEBUG << "Acceptor::Acceptor() socket failed." << std::endl;
    return;
  }
  int opt = 1;

  // 允许端口在关闭后被快速重用
  if (::setsockopt(accept_socketFd_, SOL_SOCKET, SO_REUSEADDR, &opt,
                   sizeof(opt)) < 0) {
    // 处理错误
    LOG_DEBUG << "Acceptor::Acceptor() setsockopt SO_REUSEADDR failed."
              << std::endl;
  }

  accept_channel_->setReadCallback([this]() { handleRead(); });
}
Acceptor::~Acceptor() {
  accept_channel_->remove(); // 从 EventLoop 移除
  if (accept_socketFd_ >= 0) {
    ::close(accept_socketFd_); // 关闭 listen fd
  }
}

void Acceptor::listen() {
  if (::bind(accept_socketFd_,
             reinterpret_cast<const sockaddr *>(listen_addr_.getSockAddrInet()),
             sizeof(sockaddr_in)) < 0) {
    LOG_DEBUG << "Acceptor::listen() bind failed: " << strerror(errno)
              << std::endl;
    return;
  }

  if (::listen(accept_socketFd_, SOMAXCONN) <
      0) { // SOMAXCONN 是系统定义的最大监听队列长度
    LOG_DEBUG << "Acceptor::listen() listen failed: " << strerror(errno)
              << std::endl;
    return;
  }

  accept_channel_->enableReading(); // 开始监听 EPOLLIN 事件
  listening_ = true;
  LOG_DEBUG << "Acceptor listening on " << listen_addr_.toIpPort() << std::endl;
}

void Acceptor::handleRead() {
  // 调用accpet4之后，客户端的地址会被存储在peer_addr中
  // 有客户端请求连接，这时候会再产生一个socket_fd,
  // 这个socket_fd用conn_fd存起来分发给subReactor
  // 之后就和Acceptor没有关系了，所有和这个客户端的收发数据都在分发的subReactor里
  InetAddress peer_addr;
  socklen_t len = sizeof(sockaddr_in);
  int conn_fd =
      ::accept4(accept_socketFd_,
                reinterpret_cast<sockaddr *>(
                    const_cast<sockaddr_in *>(peer_addr.getSockAddrInet())),
                &len, SOCK_NONBLOCK | SOCK_CLOEXEC);
  if (conn_fd >= 0) {
    // new_conn_callback_存放了回调函数，该回调由Server设置
    // 回调的内容是 Server的 newConnection(sockfd, peerAddr)
    if (new_conn_callback_) {
      new_conn_callback_(conn_fd, peer_addr);
    }
    // 没有处理，关闭
    else {
      ::close(conn_fd);
    }
  } else {
    // 处理 accept 错误，如 EAGAIN, EWOULDBLOCK, EMFILE 等
    LOG_DEBUG << "Acceptor::handleRead() accept failed: " << strerror(errno)
              << std::endl;
  }
}

} // namespace net