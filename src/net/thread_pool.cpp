#include "thread_pool.h"
#include <cassert>
#include <iostream>
namespace net{

// 为TcpServer提供服务，处理连接由TcpServer操作

threadPool::threadPool(EventLoop* baseLoop, int numThreads)
    :baseLoop_(baseLoop), numThreads_(numThreads),  
    is_started_(false), next_(0){ // numThreads为0的话退化成单线程
    threads_.reserve(numThreads_);
    loops_.reserve(numThreads_);
}
threadPool::~threadPool(){
    LOG_DEBUG << "ThreadPool destructing..." << std::endl;

    for(auto& loop: loops_){
        if(loop){
            loop->quit();
        }
    }

    for(auto& thread : threads_){
        if(thread){
          thread->join();
        }
    }
    LOG_DEBUG << "ThreadPool destructed." << std::endl;
}
void threadPool::start(){
    if(!is_started_){
        if(numThreads_ == 0){
            loops_.emplace_back(baseLoop_);
        }
        else{
            for(int i = 0;i < numThreads_;i++)
            {
                auto thread = std::make_unique<EventLoopThread>();
                EventLoop* loop = thread->startLoop(); 
                threads_.emplace_back(std::move(thread));
                loops_.emplace_back(loop);
            }
            LOG_DEBUG << "ThreadPool started with " << numThreads_ << " threads." << std::endl;
        }
        is_started_ = true;
    }
}
// 轮询分发是单线程操作，不加锁
EventLoop* threadPool::getNextLoop(){
    if(!is_started_) assert("is_started_");
    if(numThreads_ == 0){
        return baseLoop_;
    }
    // Acceptor监听新连接是单线程，这里可以不加锁
    // 但后续若考虑多线程并发调用最好加锁,或者设为原子操作
    // std::lock_guard<std::mutex> lock(mutex_); // 加锁
    next_ %= loops_.size();
    return loops_[next_++];
}

}