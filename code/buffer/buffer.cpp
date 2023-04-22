#include "buffer.h"
// 初始化缓冲区大小
Buffer::Buffer(int initBuffSize) : buffer_(initBuffSize), readPos_(0), writePos_(0)
{
}

// 可以读的数据的大小  写位置 - 读位置，中间的数据就是可以读的大小
size_t Buffer::ReadableBytes() const
{
    return writePos_ - readPos_;
}

// 可以写的数据大小，缓冲区的总大小 - 写位置
size_t Buffer::WritableBytes() const
{
    return buffer_.size() - writePos_;
}

// 前面可以用的空间，当前读取到哪个位置，就是前面可以用的空间大小
size_t Buffer::PrependableBytes() const
{
    return readPos_;
}

// 查看缓冲区起始地址
const char *Buffer::Peek() const
{
    return BeginPtr_() + readPos_;
}
// 移动读指针,表示读取len个字节
void Buffer::Retrieve(size_t len)
{
    assert(len <= ReadableBytes());
    readPos_ += len;
}

//buff.RetrieveUntil(lineEnd + 2);
//读取直到遇到end指针位置
void Buffer::RetrieveUntil(const char *end)
{
    assert(Peek() <= end);
    Retrieve(end - Peek());
}

// 清空缓冲区,读写指针都置0
void Buffer::RetrieveAll()
{
    bzero(&buffer_[0], buffer_.size());
    readPos_ = 0;
    writePos_ = 0;
}

// 读取全部数据并转换为std::string返回
std::string Buffer::RetrieveAllToStr()
{
    std::string str(Peek(), ReadableBytes());
    RetrieveAll();
    return str;
}

// 获取写指针地址
const char *Buffer::BeginWriteConst() const
{
    return BeginPtr_() + writePos_;
}

char *Buffer::BeginWrite()
{
    return BeginPtr_() + writePos_;
}

// 移动写指针,表示写入len个字节
void Buffer::HasWritten(size_t len)
{
    writePos_ += len;
}

// 向缓冲区尾部追加len个字节
void Buffer::Append(const std::string &str)
{
    Append(str.data(), str.length());
}

void Buffer::Append(const void *data, size_t len)
{
    assert(data);
    Append(static_cast<const char *>(data), len);
}

//  Append(buff, len - writable);   buff临时数组，len-writable是临时数组中的数据个数
void Buffer::Append(const char *str, size_t len)
{
    assert(str);
    EnsureWriteable(len);
    std::copy(str, str + len, BeginWrite());
    HasWritten(len);
}

void Buffer::Append(const Buffer &buff)
{
    Append(buff.Peek(), buff.ReadableBytes());
}

// 确保缓冲区至少有len个字节的可写空间,否则调用MakeSpace_扩容
void Buffer::EnsureWriteable(size_t len)
{
    if (WritableBytes() < len) {
        // 创建新的空间
        MakeSpace_(len);
    }
    assert(WritableBytes() >= len);
}
// 从文件描述符fd读取数据至缓冲区
ssize_t Buffer::ReadFd(int fd, int *saveErrno)
{

    char buff[65535]; // 临时的数组，保证能够把所有的数据都读出来
    /*
        - iov_base 指向某一内存块的起始地址
        - iov_len 指定该内存块的大小
        在Socket通信中,可以通过readv/writev函数在一次系统调用中读/写多个非连续缓冲区,这些缓冲区通过iovec结构体数组传入。
        其中,iov[0].iov_base是Buffer可写缓冲区的起始地址,iov[0].iov_len是其大小;
        iov[1].iov_base是临时buff数组的起始地址,iov[1].iov_len是其大小。
        所以,这个iov数组使得ReadFd可以通过一次readv调用从文件描述符中读取两段非连续的数据至Buffer:
        1. 首先读取可写缓冲区大小的数据至Buffer;
        2. 如果可写空间不足,继续读取临时buff数组大小的数据至buff;
        这避免了多次read调用的开销,提高了效率。
        举个例子:
        假设文件描述符数据为:abcdefghijk,Buffer大小为10,当前可写空间为3。
        则执行readv后:
        - iov[0]读取abc至Buffer;
        - iov[1]读取defgh至buff;
        如果再直接读取,则只能读取abc,还需再次read获取defgh,这需要两次系统调用,开销更大。
        而readv只需要一次系统调用即可获取所有数据,这节约了开销,提高了效率。
    */
    struct iovec iov[2];

    const size_t writable = WritableBytes();

    /* 分散读， 保证数据全部读完 */
    iov[0].iov_base = BeginPtr_() + writePos_;
    iov[0].iov_len = writable;
    iov[1].iov_base = buff;
    iov[1].iov_len = sizeof(buff);

    const ssize_t len = readv(fd, iov, 2);
    if (len < 0) {
        *saveErrno = errno;
    } else if (static_cast<size_t>(len) <= writable) {
        //writable 可写字节数 读到字节数len<=writable
        writePos_ += len;
    } else {
        // 装不下 用临时buff装剩下数据
        writePos_ = buffer_.size();
        Append(buff, len - writable);
    }
    return len;
}

// 将缓冲区的数据写入文件描述符fd
ssize_t Buffer::WriteFd(int fd, int *saveErrno)
{
    size_t readSize = ReadableBytes();
    ssize_t len = write(fd, Peek(), readSize);
    if (len < 0) {
        *saveErrno = errno;
        return len;
    }
    readPos_ += len;
    return len;
}

char *Buffer::BeginPtr_()
{
    return &*buffer_.begin();
}

const char *Buffer::BeginPtr_() const
{
    return &*buffer_.begin();
}

//扩充缓冲区空间
void Buffer::MakeSpace_(size_t len)
{
    if (WritableBytes() + PrependableBytes() < len) {
        buffer_.resize(writePos_ + len + 1);
    } else {
        size_t readable = ReadableBytes();
        std::copy(BeginPtr_() + readPos_, BeginPtr_() + writePos_, BeginPtr_());
        readPos_ = 0;
        writePos_ = readPos_ + readable;
        assert(readable == ReadableBytes());
    }
}