#include "net/channel.h"
#include "net/event_loop.h"
#include <cassert>
#include <iostream>
#include <unistd.h>

namespace net {

Channel::Channel(EventLoop *loop, int fd)
    : loop_(loop), 
      fd_(fd), 
      events_(0), 
      revents_(0), 
      index_(-1) {
}

Channel::~Channel() {
  // fd的close由外部管理
}

void Channel::setReadCallback(EventCallback cb) {
  read_callback_ = std::move(cb);
}

void Channel::setWriteCallback(EventCallback cb) {
  write_callback_ = std::move(cb);
}

void Channel::setCloseCallback(EventCallback cb) {
  close_callback_ = std::move(cb);
}

void Channel::setErrorCallback(EventCallback cb) {
  error_callback_ = std::move(cb);
}

void Channel::enableReading() {
  events_ |= EPOLLIN;
  update();
}

void Channel::enableWriting() {
  events_ |= EPOLLOUT;
  update();
}

void Channel::disableWriting() {
  events_ &= ~EPOLLOUT;
  update();
}

void Channel::disableAll() {
  events_ = 0;
  update();
}

void Channel::handleEvent() {
  if (revents_ & (EPOLLERR | EPOLLHUP)) {
    if (error_callback_) error_callback_();
  }
  if (revents_ & EPOLLIN) {
    if (read_callback_) read_callback_();
  }
  if (revents_ & EPOLLOUT) {
    if (write_callback_) write_callback_();
  }
}

void Channel::update() { 
  loop_->updateChannel(this); 
}

void Channel::remove() {
  assert(isNoneEvent());
  loop_->removeChannel(this);
}

bool Channel::isNoneEvent() const { 
  return events_ == 0; 
}

bool Channel::isReading() const { 
  return events_ & EPOLLIN; 
}

bool Channel::isWriting() const { 
  return events_ & EPOLLOUT; 
}

} // namespace net