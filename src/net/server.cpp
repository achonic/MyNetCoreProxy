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
    threadPool_->start();
    acceptor_->listen();
    LOG_DEBUG << "Server [" << hostport_ << "] started" << std::endl;
  }
}

void Server::newConnection(int sockfd, const InetAddress &peerAddr) {
  LOG_DEBUG << "Server::newConnection - new connection [" << peerAddr.toIpPort()
            << "] fd=" << sockfd << std::endl;

  EventLoop *ioLoop = threadPool_->getNextLoop();
  std::string connName = "conn_" + std::to_string(sockfd);

  ioLoop->runInLoop([this, ioLoop, connName, sockfd, peerAddr]() {
    auto conn =
        std::make_unique<TcpConnection>(ioLoop, connName, sockfd, peerAddr);

    // Inject handler if factory is set
    if (handlerFactory_) {
      conn->setHandler(handlerFactory_(conn.get()));
    }

    conn->setConnectionCallback(connectionCallback_);
    conn->setMessageCallback(messageCallback_);
    conn->setWriteCompleteCallback(writeCompleteCallback_);
    conn->setCloseCallback(
        [this](const TcpConnection *conn_ptr) { removeConnection(conn_ptr); });

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