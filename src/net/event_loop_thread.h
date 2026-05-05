#pragma once
#include "utils/noncopyable.h"
#include "event_loop.h"
#include <thread>
#include <mutex>
#include <condition_variable>

namespace net{
class EventLoopThread : private utils::NonCopyable{
public:
    EventLoopThread();
    ~EventLoopThread();

    EventLoop* startLoop();

    void stop();
    void join();
private:
    void threadFunc();

    // [FIX] 删除了成员变量 EventLoop loop_;
    // 原因: 工作线程应该运行自己栈上的 loop 对象，成员变量 loop_ 会导致线程 ID 冲突和生命周期混乱。
    EventLoop* loop_ptr_; // 用于传递给主线程，指向 threadFunc 栈上的 loop 对象

    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable condition_variable_;
    bool exiting_;
};
}