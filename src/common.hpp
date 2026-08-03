// 冲奖王国 —— 公共网络工具
#pragma once
#include <string>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

// 发送一行文本（自动补 \n），失败返回 false
inline bool sendLine(int fd, const std::string &s) {
    std::string t = s + "\n";
    size_t off = 0;
    while (off < t.size()) {
        ssize_t n = ::send(fd, t.data() + off, t.size() - off, 0);
        if (n <= 0) return false;
        off += (size_t)n;
    }
    return true;
}
