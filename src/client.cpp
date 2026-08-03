// 冲奖王国 · 客户端
// 用法: ./client <服务器IP> [端口=5555] [昵称]
#include "common.hpp"
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

int main(int argc, char **argv) {
    netInit();
    if (argc < 2) {
        printf("用法: ./client <服务器IP> [端口=5555] [昵称]\n");
        return 1;
    }
    const char *host = argv[1];
    int port = argc > 2 ? atoi(argv[2]) : 5555;
    std::string name = argc > 3 ? argv[3] : "";
    while (name.empty()) {
        printf("请输入昵称: ");
        fflush(stdout);
        if (!std::getline(std::cin, name)) return 0;
    }

    sock_t fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        printf("无效的IP地址: %s\n", host);
        return 1;
    }
    if (connect(fd, (sockaddr *)&addr, sizeof addr) < 0) {
        perror("连接失败");
        return 1;
    }
    printf("已连接 %s:%d, 昵称: %s\n提示: 看到 [ASK] 开头的消息时按提示输入; 其他时候输入的内容会作为聊天发出。\n",
           host, port, name.c_str());
    sendLine(fd, "JOIN " + name);

    std::thread recvThread([fd] {
        std::string buf;
        char tmp[4096];
        while (true) {
            ssize_t n = recv(fd, tmp, (int)sizeof tmp, 0);
            if (n <= 0) {
                printf("\n[与服务器断开连接]\n");
                exit(0);
            }
            buf.append(tmp, (size_t)n);
            size_t pos;
            while ((pos = buf.find('\n')) != std::string::npos) {
                std::string line = buf.substr(0, pos);
                buf.erase(0, pos + 1);
                if (line.rfind("[ASK] ", 0) == 0)
                    printf("\033[1;33m%s\033[0m\n", line.c_str()); // 高亮提问
                else
                    printf("%s\n", line.c_str());
                fflush(stdout);
            }
        }
    });
    recvThread.detach();

    std::string line;
    while (std::getline(std::cin, line)) sendLine(fd, line);
    closesock(fd);
    return 0;
}
