#pragma once

#include <vector>
#include <string>
#include <algorithm>
#include <assert.h>
#include <sys/uio.h>
#include "utils/noncopyable.h"
namespace net{

class Buffer : private utils::NonCopyable{
public:
    //预留 8 字节空间在缓冲区开头，
    //用于可能的协议头（如长度字段）前置操作，避免数据拷贝。
    static const size_t cacheHeapPrepend_ = 8; 
    // 初始缓冲区大小
    static const size_t initialSize_ = 1024;

    explicit Buffer(size_t initialSize = initialSize_);

    size_t readableBytes() const;
    size_t writableBytes() const;
    // 前置可写空间量（通常用于协议头）
    size_t prependableBytes() const;

    const char* peek() const;

    char* beginwrite();
    const char* beginwrite() const;

    void retrieve(size_t len);
    void retrieveUntil(const char* end);
    void retrieveAll();

    std::string retrieveAllAsString();
    std::string retrieveAsString(size_t len);
    std::string peekAsString(size_t len) const;

    void ensureWriteableBytes(size_t len);

    void append(const char* data, size_t len);
    void append(const std::string& str);
    void append(const void* data, size_t len);

    void prepend(const void* data, size_t len);

    ssize_t readFd(int fd, int* savedErrno);
private:
    char* begin();
    const char* begin() const;

    void makeSpace(size_t len);

    std::vector<char> buffer_;
    size_t readerIndex_;
    size_t writerIndex_;
};

}