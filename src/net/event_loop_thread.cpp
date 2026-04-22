#include "net/event_loop_thread.h"

namespace net {

EventLoopThread::EventLoopThread() : loop_ptr_(nullptr), exiting_(false) {}

// 确保所有线程退出，如果线程还在执行，就让它执行完
// 调用join的线程会阻塞，等待被调用join的线程执行结束后才往下执行
EventLoopThread::~EventLoopThread() {
  if (thread_.joinable()) {
    exiting_ = true;
    loop_.quit();
    thread_.join();
  }
}

EventLoop *EventLoopThread::startLoop() {
  // 这里建一个worker线程的操作，是让worker线程运行loop事件循环，执行任务
  // 这里主线程则将loop的地址返回给创建这个startloop的主线程Eventloop

  thread_ = std::thread(&EventLoopThread::threadFunc, this);
  // 加锁
  // 同步互斥操作，mutex是对loop_ptr_原子操作，condition_variable_让notify来唤醒wait
  std::unique_lock<std::mutex> lock(mutex_);

  // 防止loop_ptr_返回nullptr，确保loop_ptr_已经被赋值
  condition_variable_.wait(lock, [this]() { return loop_ptr_ != nullptr; });

  return loop_ptr_;
}
void EventLoopThread::stop() { loop_.quit(); }
void EventLoopThread::join() {
  if (thread_.joinable()) {
    thread_.join();
  }
}
// worker线程
void EventLoopThread::threadFunc() {
  EventLoop loop;
  {
    // 加锁 让对变量loop_ptr_的访问是原子操作
    std::lock_guard<std::mutex> lock(mutex_);
    loop_ptr_ = &loop;
  }

  condition_variable_.notify_one();

  loop.loop();
}
} // namespace net
