#pragma once

namespace net {

class TcpConnection;
class Buffer;

// Abstract interface for handling application-level logic on a TcpConnection.
// Implementations handle protocol parsing, forwarding, etc.
// TcpConnection handles only low-level networking (I/O, buffers, connection
// lifecycle).
class ConnectionHandler {
public:
  virtual ~ConnectionHandler() = default;

  // 当连接的输入缓冲区有数据时调用
  virtual void onMessage(TcpConnection *conn, Buffer *buffer) = 0;

  // 当连接完全建立时调用
  virtual void onConnection(TcpConnection *conn) = 0;

  // 当连接关闭时调用
  virtual void onClose(TcpConnection *conn) {}
};

} // namespace net
