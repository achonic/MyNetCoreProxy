#pragma once

#include "net/connection_handler.h"
#include "net/tcp_connection.h"
// Removed UDS dependency
#include "net/inet_address.h"
#include <memory>
#include <string>

namespace app {
// 除去了和go的通信，完全本地化
// Handles HTTP proxy logic: parses HTTP headers, communicates with Go control
// plane via UDS, creates backend connections, and sets up bidirectional relay.
class HttpProxyHandler : public net::ConnectionHandler {
public:
  // 路由表：key 为请求路径，value 为后端地址
  // 显式表示，一定要传入路由表，否则无法工作
  explicit HttpProxyHandler(
      const std::unordered_map<std::string, std::string> &routing_table);
  ~HttpProxyHandler() override = default;

  void onMessage(net::TcpConnection *conn, net::Buffer *buffer) override;
  void onConnection(net::TcpConnection *conn) override;
  void onClose(net::TcpConnection *conn) override;

  // Local routing resolution
  void startForwarding(net::TcpConnection *conn, const std::string &path);

private:
  void createBackendConnection(net::TcpConnection *frontend_conn,
                               const net::InetAddress &backend_addr,
                               const std::string &backend_addr_str);

  void setupBidirectionalRelay(net::TcpConnection *frontend_conn,
                               const std::string &backend_addr);

  // HTTP parsing helpers
  static std::string parseHeader(const std::string &req,
                                 const std::string &key);
  static std::string parsePath(const std::string &req);
  static std::string modifyRequestForBackend(const std::string &request,
                                             const std::string &backend_addr);

  std::unordered_map<std::string, std::string> routing_table_;
  bool waiting_for_initial_data_;
  std::shared_ptr<net::TcpConnection> backend_conn_;
};

} // namespace app
