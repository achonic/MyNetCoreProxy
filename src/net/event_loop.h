#pragma once

#include "utils/noncopyable.h"
#include "channel.h"
#include <thread>
#include <vector>
#include <sys/epoll.h>
#include <memory>
#include <deque>
#include <mutex> 
#include <atomic>

namespace net{

class EventLoop : private utils::NonCopyable{
public:
    EventLoop();
    ~EventLoop();

    // 事件循环提供的服务 1.启动 2.退出
    void loop();
    void quit();

    // 是否在循环事件所属线程
    bool isInLoopThread() const;
    std::thread::id getThreadId() const { return thread_id_; }

    void wakeup();
    void runInLoop(std::function<void()> cb);
    void updateChannel(Channel* channel);
    void removeChannel(Channel* channel);

private:
//    void handleEpollEvent();

    const int kEpollFdSize = 1024;

    int epoll_fd_;
    bool looping_;
    bool quit_;

    int wakeup_fd_;
    std::unique_ptr<Channel> wakeupChannel_;

    std::thread::id thread_id_;
    std::vector<struct epoll_event> events_;
    void handleWakeup();
    
    std::mutex mutex_; // 保护任务队列互斥锁
    std::deque<std::function<void()>> pending_functors_;
    std::atomic<bool> calling_pending_functors_;

    void doPendingFunctors();

//    void update(int operation, Channel* channel);
};
}