#pragma once
#include "utils/noncopyable.h"
#include <functional>
#include <sys/epoll.h>

// 封装fd和读写回调处理在Channel中 让Eventloop处理epoll事件循环、调度
// 不直接调用epoll_ctl，由EventLoop间接调用
namespace net {

class EventLoop;

class Channel : private utils::NonCopyable {
public:
  using EventCallback = std::function<void()>;

  Channel(EventLoop *loop, int fd);
  ~Channel();

  // 读写回调
  // Channel 只负责分发事件。当 epoll 告诉它 fd 上有可读事件时
  //  就调用 read_callback_()
  void setReadCallback(EventCallback cb);
  void setWriteCallback(EventCallback cb);
  void setCloseCallback(EventCallback cb);
  void setErrorCallback(EventCallback cb);

  // 开启读，注册读事件到Epoll当中
  void enableReading();
  // 关闭读
  void disableReading();
  // 开启写，注册写事件到Epoll当中
  void enableWriting();
  // 关闭写
  void disableWriting();
  void disableAll();
  
  // 状态查询
  bool isReading() const;
  bool isWriting() const;
  bool isNoneEvent() const;

  // Getter/Setter
  int fd() const { return fd_; }
  uint32_t events() const { return events_; }
  void setRevents(uint32_t revents) { revents_ = revents; }
  
  int index() const { return index_; }
  void setIndex(int index) { index_ = index; }

  EventLoop *ownerLoop() const { return loop_; }

  // 事件处理与生命周期管理
  void handleEvent();
  void remove();

private:
  void update();

  EventLoop *loop_;
  const int fd_;
  uint32_t events_;
  uint32_t revents_;
  int index_;

  EventCallback read_callback_;
  EventCallback write_callback_;
  EventCallback close_callback_;
  EventCallback error_callback_;
};

} // namespace net