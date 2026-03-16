#pragma once

#include <iostream>

// 如果定义了 NDEBUG（Release 模式标准宏），则关闭日志
#ifdef NDEBUG
    // 核心黑科技：if(0) 会让编译器在优化时直接把这一整行代码连同后面的参数计算全部丢弃！
    // 不会产生任何运行时开销！
    #define LOG_DEBUG if(0) std::cout 
#else
    // Debug 模式下，正常输出到控制台
    #define LOG_DEBUG std::cout
#endif

namespace utils{
class NonCopyable{
protected:
    NonCopyable() = default;
    ~NonCopyable() = default;
    NonCopyable(const NonCopyable&) = delete;
    NonCopyable& operator=(const NonCopyable&) = delete;
};
}