#include "server.h"

namespace net {

Server::Server(EventLoop *loop, const InetAddress &listenAddr)
    : loop_(loop), hostport_(listenAddr.toIpPort()),
      acceptor_(std::make_unique<Acceptor>(loop, listenAddr)),
      threadPool_(std::make_unique<threadPool>(loop, 4)), started_(0),
      mutex_() {
  // 有新连接后，要处理的事件
  acceptor_->setNewConnectCallback(
      [this](int sockfd, const InetAddress &peerAddr) {
        newConnection(sockfd, peerAddr);
      });
}

Server::~Server() {
  LOG_DEBUG << "Server::~Server [" << hostport_ << "] destructing" << std::endl;
}

void Server::start() {
  if (started_.exchange(1) == 0) {

    // 此步执行完后，worker线程池就已经都在跑loop，无epoll事件就阻塞在epoll_wait
    // 此时loop的epoll还未注册其他fd事件，仅有一个wakeup_fd_。
    threadPool_->start();

    // acceptor 此时就可以监听客户端的连接了
    acceptor_->listen();

    LOG_DEBUG << "Server [" << hostport_ << "] started" << std::endl;
  }
}

// 从线程池取 worker 线程，然后把TcpConnection对象创建任务投递给 worker 线程
//
void Server::newConnection(int sockfd, const InetAddress &peerAddr) {
  LOG_DEBUG << "Server::newConnection - new connection [" << peerAddr.toIpPort()
            << "] fd=" << sockfd << std::endl;
  // 当前是主线程，workerLoop 是 worker 线程池取出来的其中一个 worker
  // 线程事件循环对象指针
  EventLoop *workerLoop = threadPool_->getNextLoop();
  std::string connName = "conn_" + std::to_string(sockfd);

  // 执行 worker 事件循环里 runInLoop 的是主线程
  // 属于跨线程行为，主线程不能处理 worker 线程的任务，只能投递
  // 所以这里是主线程通过持有 worker 事件循环对象的指针
  // 把创建 TcpConnection 对象任务投递给 worker 线程的任务队列
  workerLoop->runInLoop([this, workerLoop, connName, sockfd, peerAddr]() {
    auto conn =
        std::make_unique<TcpConnection>(workerLoop, connName, sockfd, peerAddr);

    // Inject handler if factory is set
    if (handlerFactory_) {
      conn->setHandler(handlerFactory_(conn.get()));
    }

    conn->setConnectionCallback(connectionCallback_);
    conn->setMessageCallback(messageCallback_);
    conn->setWriteCompleteCallback(writeCompleteCallback_);
    conn->setCloseCallback(
        [this](const TcpConnection *conn_ptr) { removeConnection(conn_ptr); });

    // worker线程会消费 connections_.erase(connName)
    {
      std::lock_guard<std::mutex> lock(mutex_);
      connections_[connName] = std::move(conn);
    }
    connections_[connName]->connectEstablished();
  });
}

void Server::removeConnection(const TcpConnection *conn) {
  LOG_DEBUG << "Server::removeConnection [" << conn->name()
            << "] - connection closed" << std::endl;
  // C++14 广义 lambda 捕获
  loop_->runInLoop([this, connName = std::string(conn->name())]() {
    std::lock_guard<std::mutex> lock(mutex_);
    size_t n = connections_.erase(connName);
    (void)n;
    assert(n == 1);
  });
}

std::shared_ptr<TcpConnection> Server::findConnectionByFd(int conn_fd) {
  std::lock_guard<std::mutex> lock(mutex_);
  for (auto &pair : connections_) {
    if (pair.second->get_fd() == conn_fd) {
      return pair.second;
    }
  }
  return nullptr;
}

} // namespace net