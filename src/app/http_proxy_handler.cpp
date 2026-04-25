#include "http_proxy_handler.h"
#include "net/buffer.h"
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <sys/socket.h>
#include <unistd.h>

using namespace net;
namespace app {

HttpProxyHandler::HttpProxyHandler(
    const std::unordered_map<std::string, std::string> &routing_table)
    : routing_table_(routing_table), waiting_for_initial_data_(true),
      backend_conn_(nullptr) {}

// 设置消息传输的“连接”和“转发”两个阶段的逻辑
void HttpProxyHandler::onMessage(net::TcpConnection *conn,
                                 net::Buffer *buffer) {
  if (waiting_for_initial_data_) {
    // 1. 数据 Peek，转成 string 方便查找，但 buffer 内部读指针不动！
    std::string preview(buffer->peek(), buffer->readableBytes());

    // 解析
    std::string host = parseHeader(preview, "Host");
    std::string path = parsePath(preview);
    if (host.empty() || path.empty()) {
      // 如果 preview 很大了（比如 > 4k）还没有 Host，可能是恶意攻击，直接关掉
      return;
    }

    // Route lookup based on static table
    startForwarding(conn, path);
  } else {
    // 数据已进入转发模式，通过 message_callback_ 处理（由
    // setupBidirectionalRelay 设置） 此时 conn 的 message_callback_ 已被
    // setupBidirectionalRelay 设置为转发逻辑 但因为现在 handler
    // 自己管理这个逻辑，我们直接在这里转发
    if (backend_conn_ && backend_conn_->connected()) {
      LOG_DEBUG
          << "[" << conn->name()
          << "] Backend connection is ready, proceeding to modify and send."
          << std::endl;
      std::string request = buffer->retrieveAllAsString();
      // 这里需要 backend_addr，但我们在 setupBidirectionalRelay 中已经保存了
      // 通过 backend_conn_ 直接 send
      backend_conn_->send(request);
      LOG_DEBUG << "[" << conn->name() << "] Sent " << request.length()
                << " bytes to backend connection." << std::endl;
    } else {
      LOG_DEBUG << "[" << conn->name() << "] Backend not ready, data buffered ("
                << buffer->readableBytes() << " bytes)." << std::endl;
    }
  }
}

void HttpProxyHandler::onConnection(net::TcpConnection *conn) {
  // 连接建立时的处理逻辑（目前无需额外操作）
}

void HttpProxyHandler::onClose(net::TcpConnection *conn) {
  // 前端连接关闭时，同时关闭后端连接
  if (backend_conn_) {
    backend_conn_.reset();
  }
}

// Directly create the backend connection based on the matched route
void HttpProxyHandler::startForwarding(net::TcpConnection *conn,
                                       const std::string &path) {
  std::string backend_addr = "";

  // Exact match lookup
  auto it = routing_table_.find(path);
  if (it != routing_table_.end()) {
    backend_addr = it->second;
  } else {
    // Fallback or 404
    auto default_it = routing_table_.find("/");
    if (default_it != routing_table_.end()) {
      backend_addr = default_it->second;
    } else {
      LOG_DEBUG << "[" << conn->name() << "] No route found for path: " << path
                << std::endl;
      // Send 404 and close
      std::string not_found_response =
          "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: "
          "close\r\n\r\n";
      conn->send(not_found_response);
      conn->shutdown();
      return;
    }
  }

  LOG_DEBUG << "Starting forwarding from " << conn->name() << " for path "
            << path << " to " << backend_addr << std::endl;

  // 解析 backend_addr (例如 "127.0.0.1:9000")
  size_t pos = backend_addr.find(':');
  if (pos == std::string::npos) {
    std::cerr << "Invalid backend address: " << backend_addr << std::endl;
    return;
  }

  std::string ip = backend_addr.substr(0, pos);
  int port;
  try {
    port = std::stoi(backend_addr.substr(pos + 1));
  } catch (...) {
    std::cerr << "Invalid port from Go: " << backend_addr << std::endl;
    return;
  }

  InetAddress backend_addr_obj(ip, port);

  // 交由Eventloop的线程处理IO事件
  conn->getLoop()->runInLoop([this, conn, backend_addr_obj, backend_addr]() {
    createBackendConnection(conn, backend_addr_obj, backend_addr);
  });
}

void HttpProxyHandler::createBackendConnection(
    net::TcpConnection *frontend_conn, const net::InetAddress &backend_addr,
    const std::string &backend_addr_str) {
  // 创建 socket
  int backend_sock = socket(AF_INET, SOCK_STREAM, 0);
  if (backend_sock < 0) {
    std::cerr << "Failed to create backend socket" << std::endl;
    return;
  }

  // 设置非阻塞
  int flags = fcntl(backend_sock, F_GETFL, 0);
  fcntl(backend_sock, F_SETFL, flags | O_NONBLOCK);

  // 连接到后端
  const struct sockaddr *addr_ptr =
      reinterpret_cast<const sockaddr *>(backend_addr.getSockAddrInet());
  socklen_t addr_len = sizeof(struct sockaddr_in);

  int ret = connect(backend_sock, addr_ptr, addr_len);

  if (ret < 0) {
    if (errno != EINPROGRESS) {
      std::cerr << "Failed to connect to backend: " << strerror(errno)
                << std::endl;
      close(backend_sock);
      return;
    }
    // EINPROGRESS: 连接进行中，需要监听可写事件
  }

  // 创建后端 TcpConnection（注意：不设 handler，后端连接用 callback 模式）
  std::string backend_name = frontend_conn->name() + "_backend";
  auto backend_conn = std::make_shared<net::TcpConnection>(
      frontend_conn->getLoop(), backend_name, backend_sock);

  // 保存后端连接
  backend_conn_ = backend_conn;

  // 设置双向转发
  setupBidirectionalRelay(frontend_conn, backend_addr_str);

  backend_conn_->connectEstablishedAsClient();
}

void HttpProxyHandler::setupBidirectionalRelay(
    net::TcpConnection *frontend_conn, const std::string &backend_addr) {
  LOG_DEBUG << "Setting up bidirectional relay for " << frontend_conn->name()
            << " to backend: " << backend_addr << std::endl;

  // 设置后端连接的回调（后端数据 → 客户端）
  if (backend_conn_) {
    // ---- 闭环流控（全链路背压） ----
    // 1. 后端数据积压超过水位，让前端停读
    backend_conn_->setHighWaterMarkCallback(
        [frontend_conn](TcpConnection *conn) {
          LOG_DEBUG << "[" << frontend_conn->name() << "] Backend high water mark reached. Pausing frontend reading." << std::endl;
          frontend_conn->stopRead();
        }, 64 * 1024 * 1024);

    // 2. 后端数据发送完毕，让前端恢复读取
    backend_conn_->setWriteCompleteCallback(
        [frontend_conn](TcpConnection *conn) {
          LOG_DEBUG << "[" << frontend_conn->name() << "] Backend write complete. Resuming frontend reading." << std::endl;
          frontend_conn->startRead();
        });

    // 3. 前端数据积压超过水位，让后端停读
    frontend_conn->setHighWaterMarkCallback(
        [this](TcpConnection *conn) {
          LOG_DEBUG << "[" << conn->name() << "] Frontend high water mark reached. Pausing backend reading." << std::endl;
          if (backend_conn_) backend_conn_->stopRead();
        }, 64 * 1024 * 1024);

    // 4. 前端数据发送完毕，让后端恢复读取
    frontend_conn->setWriteCompleteCallback(
        [this](TcpConnection *conn) {
          LOG_DEBUG << "[" << conn->name() << "] Frontend write complete. Resuming backend reading." << std::endl;
          if (backend_conn_) backend_conn_->startRead();
        });
    // ---- 流控设置结束 ----

    backend_conn_->setMessageCallback(
        [this, frontend_conn](TcpConnection *backend_conn, Buffer *buffer) {
          std::string response = buffer->retrieveAllAsString();
          LOG_DEBUG << "\n=== BACKEND RESPONSE RECEIVED ===" << std::endl;
          LOG_DEBUG << "[" << frontend_conn->name() << "] Backend received "
                    << response.length() << " bytes from AList server."
                    << std::endl;

          std::string snippet =
              response.substr(0, std::min(500, (int)response.length()));
          LOG_DEBUG << "Response snippet:\n" << snippet << std::endl;
          LOG_DEBUG << "=== BACKEND RESPONSE END ===\n" << std::endl;

          if (frontend_conn->connected()) {
            LOG_DEBUG << "[" << frontend_conn->name()
                      << "] Frontend connection is connected, proceeding to "
                         "send response to client."
                      << std::endl;
            frontend_conn->send(response);
            LOG_DEBUG << "[" << frontend_conn->name() << "] Sent "
                      << response.length() << " bytes to frontend client."
                      << std::endl;
          } else {
            std::cerr << "[" << frontend_conn->name()
                      << "] ERROR: Frontend connection is not connected when "
                         "trying to send response."
                      << std::endl;
          }
        });

    // 设置后端连接建立回调
    backend_conn_->setConnectionCallback(
        [this, frontend_conn, backend_addr](TcpConnection *backend_conn_obj) {
          if (backend_conn_ && backend_conn_->connected()) {
            LOG_DEBUG << "[" << frontend_conn->name()
                      << "] Backend connection ESTABLISHED to " << backend_addr
                      << std::endl;

            // 当后端连接成功时，回头看看前端的 input_buffer_
            // 里有没有之前积压的数据
            if (frontend_conn->inputBuffer().readableBytes() > 0) {
              LOG_DEBUG << "[" << frontend_conn->name() << "] Sending "
                        << frontend_conn->inputBuffer().readableBytes()
                        << " bytes of delayed initial data to backend."
                        << std::endl;

              std::string request =
                  frontend_conn->inputBuffer().retrieveAllAsString();
              std::string modified_request =
                  modifyRequestForBackend(request, backend_addr);
              backend_conn_->send(modified_request);
            }
          } else {
            // 后端连接断开时的处理
            LOG_DEBUG << "[" << frontend_conn->name()
                      << "] Backend connection CLOSED." << std::endl;
            // 联动关闭前端 — 通过 shutdown 触发
            frontend_conn->shutdown();
          }
        });
  } else {
    std::cerr << "[" << frontend_conn->name()
              << "] ERROR: backend_conn_ is nullptr in setupBidirectionalRelay!"
              << std::endl;
  }

  waiting_for_initial_data_ = false; // 切换到转发模式
}

std::string HttpProxyHandler::parseHeader(const std::string &req,
                                          const std::string &key) {
  size_t pos = req.find(key + ": ");
  if (pos == std::string::npos)
    return "";

  size_t start = pos + key.length() + 2;
  size_t end = req.find("\r\n", start);
  if (end == std::string::npos)
    return "";

  return req.substr(start, end - start);
}

std::string HttpProxyHandler::parsePath(const std::string &req) {
  // 找第一行 GET /xxx HTTP/1.1
  size_t first_space = req.find(' ');
  if (first_space == std::string::npos)
    return "";

  size_t second_space = req.find(' ', first_space + 1);
  if (second_space == std::string::npos)
    return "";

  return req.substr(first_space + 1, second_space - first_space - 1);
}

// 需要修改host头成ip
std::string
HttpProxyHandler::modifyRequestForBackend(const std::string &request,
                                          const std::string &backend_addr) {
  std::string modified = request;

  // 修改 Host 头
  size_t host_pos = modified.find("Host: ");
  if (host_pos != std::string::npos) {
    size_t eol = modified.find("\r\n", host_pos);
    if (eol != std::string::npos) {
      std::string new_host_line = "Host: " + backend_addr + "\r\n";
      modified.replace(host_pos, eol - host_pos + 2, new_host_line);
      LOG_DEBUG << "[HttpProxyHandler] Modified Host header to: "
                << new_host_line;
    }
  }

  return modified;
}

} // namespace app
