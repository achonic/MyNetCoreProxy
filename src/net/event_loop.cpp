#include "net/event_loop.h"
#include "net/channel.h"
#include "net/timer_queue.h"
#include <cassert>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <sys/eventfd.h>
#include <unistd.h>
namespace net {

EventLoop::EventLoop()
    : epoll_fd_(::epoll_create1(EPOLL_CLOEXEC)), looping_(false), quit_(false),
      thread_id_(std::this_thread::get_id()), events_(kEpollFdSize),
      wakeup_fd_(-1), wakeupChannel_(nullptr),
      timerQueue_(std::make_unique<TimerQueue>(this)) {
  if (epoll_fd_ < 0) {
    perror("epoll_create1");
    abort();
  }
  // 创建 wakeupFd 用来唤醒当前线程的loop，通过调用方法wakeup()
  wakeup_fd_ = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  if (wakeup_fd_ < 0) {
    perror("eventfd");
    abort();
  }

  // 创建 wakeupChannel 并注册到 epoll
  // wakeup()往fd里写1个值，触发可读事件唤醒epoll_wait，从而唤醒当前线程的loop循环
  wakeupChannel_ = std::make_unique<Channel>(this, wakeup_fd_);
  wakeupChannel_->setReadCallback([this]() { handleWakeup(); });
  wakeupChannel_->enableReading(); // 只监听读事件
}

// 调用虚构后，循环结束，将epoll_fd关闭
EventLoop::~EventLoop() {
  if (epoll_fd_ >= 0) {
    ::close(epoll_fd_);
  }
  if (wakeup_fd_ >= 0) {
    ::close(wakeup_fd_);
  }
}

void EventLoop::runInLoop(std::function<void()> cb) {
  // 在当前线程，且任务队列没有在执行，那直接执行当前任务。
  if (isInLoopThread() && !calling_pending_functors_) {
    cb();
  } else {
    // 否则就把任务投递到队列里，并插入唤醒标志
    {
      std::lock_guard<std::mutex> lock(mutex_);
      pending_functors_.push_back(std::move(cb));
    }
    wakeup();
  }
}

void EventLoop::doPendingFunctors() {
  std::deque<std::function<void()>> functors;
  // 正在处理任务队列，让新投递任务不要唤醒eventloop
  calling_pending_functors_ = true;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    // 将 pending_functors_ 的内容交换到局部变量 functors
    // 避免长时间持有锁
    functors.swap(pending_functors_);
  }
  for (const auto &functor : functors) {
    functor();
  }
  calling_pending_functors_ = false;
}
void EventLoop::loop() {
  // 循环检测，线程安全检测
  assert(!looping_);
  assert(isInLoopThread());

  looping_ = true;
  quit_ = false;

  //   LOG_DEBUG << "Eventloop " << this << " started in thread " << thread_id_
  //   << std::endl;

  // 事件循环  检测退出事件，如signal的Ctrl+C退出，若触发则退出循环
  while (!quit_) {
    doPendingFunctors();
    // epoll阻塞 监听已经注册到epoll的fd的动作
    // wakeup_fd_的可读事件被注册到了epoll的事件当中
    // wakeup()向wakeup_fd_被写入数据时，epoll_wait检测到可读事件就会被唤醒
    int num_events = ::epoll_wait(epoll_fd_, events_.data(),
                                  static_cast<int>(events_.size()), 10000);
    if (num_events > 0) {
      for (int i = 0; i < num_events; i++) {
        // 有 IO 事件了，需要执行 IO 事件
        // IO 事件的执行由Channel实现
        Channel *channel = static_cast<Channel *>(events_[i].data.ptr);
        channel->setRevents(events_[i].events);
        channel->handleEvent();
      }
    } else if (num_events == 0) {
      //       LOG_DEBUG << "epoll_wait timeout\n";
    } else {
      if (errno != EINTR) {
        perror("epoll_wait");
      }
    }
  }
  looping_ = false;
  LOG_DEBUG << "EventLoop " << this << " stopped." << std::endl;
}

void EventLoop::quit() {
  quit_ = true;
  if (!isInLoopThread()) {
    wakeup();
  }
}
// 唤醒 EventLoop —— 向 wakeup_fd_ 写入一个值
void EventLoop::wakeup() {
  uint64_t one = 1;
  ssize_t n = ::write(wakeup_fd_, &one, sizeof(one));
  if (n != sizeof(one)) {
    std::cerr << "EventLoop::wakeup() wrote " << n << " bytes instead of 8"
              << std::endl;
  }
}

void EventLoop::handleWakeup() {
  uint64_t one = 0;
  ssize_t n = ::read(wakeup_fd_, &one, sizeof(one));
  if (n != sizeof(one)) {
    std::cerr << "EventLoop::handleWakeup() read " << n << " bytes instead of 8"
              << std::endl;
  }
}

bool EventLoop::isInLoopThread() const {
  return thread_id_ == std::this_thread::get_id();
}

// 用epoll_ctl,把fd注册进epoll当中，让event的ptr存储channel的地址，
// 事件由channel->events() | EPOLLET 得到, 边沿触发模式
void EventLoop::updateChannel(Channel *channel) {
  int fd = channel->fd();

  struct epoll_event event;
  event.data.ptr = channel;
  event.events = channel->events();
  int op =
      EPOLL_CTL_ADD; // 添加  EPOLL相当于一个红黑树、有添加、修改、删除三个操作
  if (::epoll_ctl(epoll_fd_, op, fd, &event) < 0) {
    if (errno == EEXIST) {
      op = EPOLL_CTL_MOD; // 已经存在就修改
      ::epoll_ctl(epoll_fd_, op, fd, &event);
    } else {
      perror("epoll_ctl");
    }
  }
}
void EventLoop::removeChannel(Channel *channel) {
  assert(channel != nullptr);
  assert(channel->ownerLoop() == this); // 确保 Channel 属于当前 EventLoop
  assert(!looping_ || isInLoopThread()); // 确保在正确的线程中调用

  if (looping_) { // 如果 EventLoop 正在运行（looping_ 为 true）
    int fd = channel->fd();
    // 检查 Channel 是否在 EventLoop 的活跃通道列表中
    // 也可以通过 Channel 的 index_ 来判断它是否在 epoll 中 (index_ >= 0)
    if (channel->index() !=
        -1) { // 假设 Channel 有一个 index_ 成员，-1 表示未注册
      // 从 epoll 中移除 fd
      if (::epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr) < 0) {
        perror("EventLoop::removeChannel epoll_ctl EPOLL_CTL_DEL");
      }
    }
    channel->setIndex(-1); // 更新 Channel 状态，表示已从 epoll 移除
  } else {
    // 如果 EventLoop 还未开始运行或已经停止，则 Channel 不会在 epoll 中
    // 只需更新 Channel 状态即可
    channel->setIndex(-1);
  }
}
/*
  每个定时任务都绑定一个 Timerid ，能够通过 Timerid 取消定时任务
  三个函数完成了：
    1、指定某一时刻执行定时任务。
    2、指定当前时间的几秒后执行定时任务。
    3、指定当前时间的interval后，周期性间隔interval执行定时任务。
*/

/*
    一次性定时任务
    时间是直接传进来的绝对时间

    runAt(Timestamp time, std::function<void()> cb)
    表示在当前时间戳执行定时任务cb。
*/
TimerId EventLoop::runAt(Timestamp time, std::function<void()> cb) {
  return timerQueue_->addTimer(std::move(cb), time, 0.0);
}

/*
  runAfter：相对延迟的一次性任务
  addTime(Timestamp::now(), delay)
  delay = 3 会返回当前时间3秒后的时间戳。然后塞入cb，表示定时3秒后执行这个cb。
*/

TimerId EventLoop::runAfter(double delay, std::function<void()> cb) {
  Timestamp time(addTime(Timestamp::now(), delay));
  return runAt(time, std::move(cb));
}

/*
  runEvery：相对延迟启动的周期任务
  周期任务在 timerQueue_->addTimer 的第三项设置 interval
  表示周期执行的时间间隔。
*/
TimerId EventLoop::runEvery(double interval, std::function<void()> cb) {
  Timestamp time(addTime(Timestamp::now(), interval));
  return timerQueue_->addTimer(std::move(cb), time, interval);
}

// cancel：按 TimerId 取消任务
void EventLoop::cancel(TimerId timerId) { return timerQueue_->cancel(timerId); }

} // namespace net