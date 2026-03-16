#include "buffer.h"
#include <unistd.h>
#include <sys/socket.h>
#include <errno.h>

namespace net{

Buffer::Buffer(size_t initialSize)
    : buffer_(cacheHeapPrepend_ + initialSize),
      readerIndex_(cacheHeapPrepend_),
      writerIndex_(cacheHeapPrepend_){
    // 构造后，readerIndex_ 和 writerIndex_ 都指向 kCheapPrepend
    // 使得 prepend 操作有空间可用

    //检测初始化是否正确
    assert(readableBytes() == 0);
    assert(writableBytes() == initialSize);
    assert(prependableBytes() == cacheHeapPrepend_);

    }
size_t Buffer::readableBytes() const {
    return writerIndex_ - readerIndex_;
}

size_t Buffer::writableBytes() const {
    return buffer_.size() - writerIndex_;
}

size_t Buffer::prependableBytes() const {
    return readerIndex_;
}

const char* Buffer::peek() const{
    return begin() + readerIndex_;
}
char* Buffer::beginwrite() {
    return begin() + writerIndex_;
}
const char* Buffer::beginwrite() const{
    return begin() + writerIndex_;
}

void Buffer::retrieve(size_t len){
    if(len < readableBytes()){
        readerIndex_ += len;
    }
    // len大于等于待读区大小，直接全部读取了
    else{
        retrieveAll();
    }

}
// retrieve 取 待读区的数据 消费数据只是移动待读指针，不实际删除内存
// makeSpace时移动未读数据 才可能进行拷贝
void Buffer::retrieveUntil(const char* end){
    assert(peek() <= end);
    assert(end <= beginwrite());
    retrieve(end - peek());
}
void Buffer::retrieveAll(){
    readerIndex_ = cacheHeapPrepend_;
    writerIndex_ = cacheHeapPrepend_;
}
std::string Buffer::retrieveAllAsString() { 
    return retrieveAsString(readableBytes());
}

std::string Buffer::retrieveAsString(size_t len){
    std::string result(peek(), len);
    retrieve(len);
    return result;
}
std::string Buffer::peekAsString(size_t len) const{
    return std::string(peek(), len);
}
void Buffer::ensureWriteableBytes(size_t len){
    if(writableBytes() < len) {
        makeSpace(len);
    }
    assert(writableBytes() >= len);
}
void Buffer::append(const char* data, size_t len){
    ensureWriteableBytes(len);
    std::copy(data, data + len, beginwrite());
    writerIndex_ += len;
}
void Buffer::append(const std::string& str){
    append(str.c_str(), str.size());
}
void Buffer::prepend(const void* data, size_t len){
    assert(len <= prependableBytes());
    readerIndex_ -= len;
    const char* d = static_cast<const char*>(data);
    std::copy(d, d + len, begin() + readerIndex_);
}
char* Buffer::begin() { 
    return buffer_.data();
}
const char* Buffer::begin() const{
    return buffer_.data();
}
void Buffer::makeSpace(size_t len){
    // 待写区 + 已读区 < 待写入的数据长度 直接扩容
    if(writableBytes() + prependableBytes() < len + cacheHeapPrepend_){
        buffer_.resize(writerIndex_ + len);
    }
    else{
        size_t readable = readableBytes();
        std::copy(begin() + readerIndex_, begin() + writerIndex_,
                    begin() + cacheHeapPrepend_);
        readerIndex_ = cacheHeapPrepend_;
        writerIndex_ = cacheHeapPrepend_+ readable;
        assert(readable == readableBytes());
    }
}
//有符号 ssize_t  ，这是buffer类对外接口，fd为待读取数据的描述符
ssize_t Buffer::readFd(int fd, int* savedErrno){
    char extrabuf[65536]; // 栈上临时缓冲区
    // 读取分成两个 缓冲区，一个是buffer_，一个是不够装时暂时存在extrabuf
    // 读取数据是读到这个vec里面，每个vec指定读入的起始指针，和缓冲区长度
    struct iovec vec[2]; 
    

    const size_t writable = writableBytes();
    vec[0].iov_base = beginwrite();
    vec[0].iov_len = writable;

    vec[1].iov_base = extrabuf;
    vec[1].iov_len = sizeof(extrabuf);
    //使用 readv (scatter-gather I/O) 是一个优化技巧。
    //如果缓冲区剩余空间足够，直接读入；否则，先读入栈上临时缓冲区 extrabuf，
    //再追加到 buffer_。这避免了为 extrabuf 预分配大块堆内存。
    const int iovcnt = (writable < sizeof(extrabuf)) ? 2 : 1;
    const ssize_t n = ::readv(fd, vec, iovcnt);

    if(n < 0){
        *savedErrno = errno;
    }
    else if(n <= writable){
        writerIndex_ += n;
    }
    else{
        writerIndex_ = buffer_.size();

        //将extrabuf剩余元素追加到buffer_中
        // append里会判断容量是否足够，如果不够会makeSpace扩容
        append(extrabuf, n - writable);
    }
    return n;
}

}