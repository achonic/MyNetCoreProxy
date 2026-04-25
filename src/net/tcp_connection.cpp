#include "tcp_connection.h"
#include "connection_handler.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <sys/socket.h>
#include <unistd.h>

namespace net {

TcpConnection::TcpConnection(EventLoop *loop, const std::string &name,
                             int connFd, const InetAddress &peer_addr)
    : loop_(loop), name_(name), conn_fd_(connFd), peer_addr_(peer_addr),
      state_(kConnecting), high_water_mark_(64 * 1024 * 1024) {
  channel_ = std::make_unique<Channel>(loop, connFd);

  channel_->setReadCallback([this]() { handleRead(); });
  channel_->setWriteCallback([this]() { handleWrite(); });
  channel_->setCloseCallback([this]() { handleClose(); });
  channel_->setErrorCallback([this]() { handleError(); });
};

TcpConnection::~TcpConnection() {
  // 确保连接在 TcpConnection 销毁前已断开
  assert(state_ == kDisConnected);
  if (conn_fd_ != -1) {
    ::close(conn_fd_);
    conn_fd_ = -1;
  }
}

void TcpConnection::setConnectionCallback(const ConnectionCallback &cb) {
  connection_callback_ = cb;
}
void TcpConnection::setMessageCallback(const MessageCallback &cb) {
  message_callback_ = cb;
}
void TcpConnection::setWriteCompleteCallback(const WriteCompleteCallback &cb) {
  write_complete_callback_ = cb;
}
void TcpConnection::setCloseCallback(const CloseCallback &cb) {
  close_callback_ = cb;
}

void TcpConnection::setHighWaterMarkCallback(const HighWaterMarkCallback &cb,
                                             size_t highWaterMark) {
  high_water_mark_callback_ = cb;
  high_water_mark_ = highWaterMark;
}

void TcpConnection::startRead() {
  loop_->runInLoop([this]() {
    if (state_ == kConnected || state_ == kConnecting) {
      if (!channel_->isReading()) {
        channel_->enableReading();
      }
    }
  });
}

void TcpConnection::stopRead() {
  loop_->runInLoop([this]() {
    if (state_ == kConnected || state_ == kConnecting) {
      if (channel_->isReading()) {
        channel_->disableReading();
      }
    }
  });
}

void TcpConnection::setHandler(std::unique_ptr<ConnectionHandler> handler) {
  handler_ = std::move(handler);
}

// 前端建立连接
void TcpConnection::connectEstablished() {
  assert(loop_->isInLoopThread());
  state_ = kConnected;
  channel_->enableReading();

  if (connection_callback_)
    connection_callback_(this);

  if (handler_)
    handler_->onConnection(this);
}

// 后端连接建立（主动 connect 到后端服务）
void TcpConnection::connectEstablishedAsClient() {
  assert(loop_->isInLoopThread());
  state_ = kConnecting;
  channel_->enableWriting();

  LOG_DEBUG << "Backend connection initiated: " << name_
            << ", waiting for connect to complete." << std::endl;
}

void TcpConnection::connectDestroyed() {
  assert(loop_->isInLoopThread());
  if (state_ == kConnected) {
    state_ = kDisConnected;
    channel_->disableAll();

    if (handler_)
      handler_->onClose(this);

    if (close_callback_)
      close_callback_(this);
  }
}

void TcpConnection::send(const std::string &message) {
  if (loop_->isInLoopThread()) {
    sendInLoop(message);
  } else {
    loop_->runInLoop([this, message]() { sendInLoop(message); });
  }
}

void TcpConnection::sendInLoop(const std::string &message) {
  assert(loop_->isInLoopThread());
  LOG_DEBUG << "[" << name_
            << "] sendInLoop: Start. Message size: " << message.size()
            << " bytes. State: " << state_ << std::endl;

  if (state_ == kConnected) {
    size_t remaining = message.size();
    bool faultError = false;

    if (!faultError && output_buffer_.readableBytes() == 0) {
      LOG_DEBUG << "[" << name_
                << "] sendInLoop: Attempting direct write to socket."
                << std::endl;
      ssize_t nwrote = ::write(conn_fd_, message.data(), message.size());
      LOG_DEBUG << "[" << name_
                << "] sendInLoop: Direct write result: " << nwrote
                << ", errno: " << (nwrote < 0 ? strerror(errno) : "N/A")
                << std::endl;

      if (nwrote >= 0) {
        remaining = message.size() - nwrote;
        if (remaining == 0 && write_complete_callback_) {
          LOG_DEBUG << "[" << name_
                    << "] sendInLoop: Entire message written, calling "
                       "write_complete_callback."
                    << std::endl;
          write_complete_callback_(this);
        }
      } else {
        if (errno != EWOULDBLOCK) {
          std::cerr << "[" << name_ << "] sendInLoop: Write failed with error: "
                    << strerror(errno) << std::endl;
          faultError = true;
        }
      }
    }

    if (!faultError && remaining > 0) {
      size_t oldLen = output_buffer_.readableBytes();
      LOG_DEBUG << "[" << name_ << "] sendInLoop: Buffering " << remaining
                << " bytes. Total buffered: " << oldLen + remaining
                << std::endl;
      output_buffer_.append(message.data() + message.size() - remaining,
                            remaining);

      if (oldLen + remaining >= high_water_mark_ && oldLen < high_water_mark_ &&
          high_water_mark_callback_) {
        LOG_DEBUG << "[" << name_ << "] sendInLoop: High water mark ("
                  << high_water_mark_ << ") reached. Triggering callback."
                  << std::endl;
        loop_->runInLoop(std::bind(high_water_mark_callback_, this));
      }

      if (!channel_->isWriting()) {
        channel_->enableWriting();
        LOG_DEBUG << "[" << name_ << "] sendInLoop: Enabled writing on channel."
                  << std::endl;
      }
    }

    if (faultError) {
      LOG_DEBUG << "[" << name_
                << "] sendInLoop: Fault error occurred, closing connection."
                << std::endl;
      handleClose();
    }
  } else {
    std::cerr << "[" << name_
              << "] sendInLoop: ERROR! State is not kConnected (" << state_
              << "). Cannot send." << std::endl;
  }

  LOG_DEBUG << "[" << name_ << "] sendInLoop: End." << std::endl;
}

void TcpConnection::shutdownInLoop() {
  assert(loop_->isInLoopThread());

  if (state_ == kConnected) {
    state_ = kDisConnecting;
  }
}

void TcpConnection::shutdown() {
  if (loop_->isInLoopThread()) {
    shutdownInLoop();
  } else {
    loop_->runInLoop([this]() { shutdownInLoop(); });
  }
}

// tcp只是一个收发数据的工具，真正的业务逻辑由handler处理
void TcpConnection::handleRead() {
  assert(loop_->isInLoopThread());
  int savedErrno = 0;

  while (true) {
    ssize_t n = input_buffer_.readFd(conn_fd_, &savedErrno);

    if (n > 0) {
      LOG_DEBUG << "[" << name_ << "] handleRead: SUCCESS - read " << n
                << " bytes. Buffer size after: "
                << input_buffer_.readableBytes() << std::endl;

      // 如果有设置了handler，则调用handler的onMessage方法
      // 把tcp接收到的数据交给handler处理
      if (handler_) {
        handler_->onMessage(this, &input_buffer_);
      } else if (message_callback_) {
        LOG_DEBUG << "[" << name_ << "] handleRead: Calling message callback."
                  << std::endl;
        message_callback_(this, &input_buffer_);
      }
    } else if (n == 0) {
      handleClose();
      break;
    } else {
      if (savedErrno == EAGAIN || savedErrno == EWOULDBLOCK) {
        break; // 已读完所有数据
      } else {
        errno = savedErrno;
        LOG_DEBUG << "TcpConnection::handleRead() failed: " << strerror(errno)
                  << std::endl;
        handleClose();
        break;
      }
    }
  }
}

void TcpConnection::handleWrite() {
  assert(loop_->isInLoopThread());
  LOG_DEBUG << "[" << name_ << "] handleWrite called. State: " << state_
            << std::endl;

  if (state_ == kConnecting) {
    LOG_DEBUG << "[" << name_
              << "] Handling EPOLLOUT for connection establishment."
              << std::endl;
    int error = 0;
    socklen_t len = sizeof(error);
    if (::getsockopt(conn_fd_, SOL_SOCKET, SO_ERROR, &error, &len) < 0) {
      std::cerr << "[" << name_ << "] getsockopt failed: " << strerror(errno)
                << std::endl;
      handleClose();
      return;
    }
    if (error == 0) {
      LOG_DEBUG << "[" << name_
                << "] Backend connection established successfully."
                << std::endl;
      state_ = kConnected;
      channel_->disableWriting();
      channel_->enableReading();

      if (connection_callback_) {
        connection_callback_(this);
      }
    } else {
      std::cerr << "[" << name_
                << "] Failed to connect to backend: " << strerror(error)
                << std::endl;
      handleClose();
    }
  } else {
    if (channel_->isWriting()) {
      while (output_buffer_.readableBytes() > 0) {
        ssize_t n = ::write(conn_fd_, output_buffer_.peek(),
                            output_buffer_.readableBytes());
        if (n > 0) {
          output_buffer_.retrieve(n);
          LOG_DEBUG << "[" << name_ << "] Wrote " << n
                    << " bytes from output buffer. Remaining: "
                    << output_buffer_.readableBytes() << std::endl;
          if (output_buffer_.readableBytes() == 0) {
            channel_->disableWriting();
            LOG_DEBUG << "[" << name_
                      << "] Disabled writing on channel as buffer is empty."
                      << std::endl;
            if (write_complete_callback_) {
              loop_->runInLoop(std::bind(write_complete_callback_, this));
            }
            break;
          }
        } else {
          if (errno == EAGAIN || errno == EWOULDBLOCK) {
            break; // 内核发送缓冲区已满
          } else {
            std::cerr << "[" << name_
                      << "] TcpConnection::handleWrite - write error: "
                      << strerror(errno) << std::endl;
            handleClose();
            break;
          }
        }
      }
    } else {
      LOG_DEBUG << "[" << name_ << "] Connection fd = " << conn_fd_
                << " is down, no more writing" << std::endl;
    }
  }
}

void TcpConnection::handleClose() {
  assert(loop_->isInLoopThread());
  state_ = kDisConnected;
  connectDestroyed();
}

void TcpConnection::handleError() {
  assert(loop_->isInLoopThread());
  LOG_DEBUG << "TcpConnection::handleError [" << name_
            << "] - fd = " << conn_fd_ << std::endl;
  handleClose();
}

} // namespace net