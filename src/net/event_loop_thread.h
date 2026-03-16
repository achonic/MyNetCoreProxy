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

    // 让 EventLoop 对象的生命周期与 EventLoopThread 对象一致
    EventLoop loop_;
    EventLoop* loop_ptr_; // 用于传递给主线程，指向 loop_ 对象

    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable condition_variable_;
    bool exiting_;
};
}