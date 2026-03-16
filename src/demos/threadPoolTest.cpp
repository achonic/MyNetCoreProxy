// src/demos/threadPoolTest.cpp
#include "net/event_loop.h"
#include "net/thread_pool.h"
#include <iostream>
#include <cassert>

int main() {
    net::EventLoop mainLoop;
    net::threadPool pool(&mainLoop, 2);
    pool.start();
    
    auto* l1 = pool.getNextLoop();
    auto* l2 = pool.getNextLoop();
    auto* l3 = pool.getNextLoop();
    
    assert(l1 != l2);
    assert(l1 == l3);
    LOG_DEBUG << "✅ EventLoopThreadPool test passed!\n";
    return 0;
}