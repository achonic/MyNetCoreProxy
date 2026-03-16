#include "utils/noncopyable.h"
#include "event_loop.h"
#include "event_loop_thread.h"
#include <vector>
#include <memory>
namespace net{

class threadPool : private utils::NonCopyable{

public:
    threadPool(EventLoop* baseLoop, int numThreads);
    ~threadPool();

    void start();
    EventLoop* getNextLoop();

private:
    EventLoop* baseLoop_; // 主Reactor
    int numThreads_;
    bool is_started_;

    // 管理EventLoopThread的生命周期
    std::vector<std::unique_ptr<EventLoopThread>> threads_;

    // 轮询，不直接暴露EventLoopThread
    std::vector<EventLoop*> loops_;
    size_t next_; // round-robin
    mutable std::mutex mutex_;

};
}