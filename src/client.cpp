// 冲奖王国 · 客户端
// 双击直接运行: 按提示输入服务器地址和昵称
// 命令行运行:   ./client [服务器IP] [端口=5555] [昵称]
#include "common.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <netdb.h>
#endif

static std::string trim(std::string s) {
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' '))
        s.pop_back();
    size_t i = 0;
    while (i < s.size() && s[i] == ' ') i++;
    return s.substr(i);
}

static void pauseBeforeExit() {
#ifdef _WIN32
    printf("按回车键退出...");
    fflush(stdout);
    std::string dummy;
    std::getline(std::cin, dummy);
#endif
}

// 连接 host:port, 支持 IP 和域名; 失败返回 BAD_SOCK
static sock_t connectTo(const std::string &host, int port) {
    addrinfo hints{}, *res = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) != 0 || !res)
        return BAD_SOCK;
    sock_t fd = socket(AF_INET, SOCK_STREAM, 0);
    if (!sockOk(fd)) { freeaddrinfo(res); return BAD_SOCK; }
    if (connect(fd, res->ai_addr, (socklen_t)res->ai_addrlen) < 0) {
        closesock(fd);
        fd = BAD_SOCK;
    }
    freeaddrinfo(res);
    return fd;
}

int main(int argc, char **argv) {
    netInit();
    printf("🏰 ===== 冲奖王国 · 玩家客户端 =====\n");

    std::string host = argc > 1 ? argv[1] : "";
    int port = argc > 2 ? atoi(argv[2]) : 5555;
    std::string name = argc > 3 ? argv[3] : "";

    sock_t fd = BAD_SOCK;
    for (;;) {
        if (host.empty()) {
            printf("服务器地址 (IP 或 IP:端口, 直接回车=127.0.0.1): ");
            fflush(stdout);
            if (!std::getline(std::cin, host)) return 0;
            host = trim(host);
            if (host.empty()) host = "127.0.0.1";
            size_t c = host.rfind(':');
            if (c != std::string::npos) {
                int p2 = atoi(host.substr(c + 1).c_str());
                if (p2 > 0) port = p2;
                host = host.substr(0, c);
            }
        }
        printf("正在连接 %s:%d ...\n", host.c_str(), port);
        fd = connectTo(host, port);
        if (sockOk(fd)) break;
        printf("❌ 连接失败, 请检查: 地址是否正确 / 房主是否已开服 / 是否同一局域网\n");
        host.clear();
        port = 5555;
    }

    while (name.empty()) {
        printf("你的昵称: ");
        fflush(stdout);
        if (!std::getline(std::cin, name)) return 0;
        name = trim(name);
    }

    printf("✅ 已连接, 昵称: %s\n提示: 看到黄色 [ASK] 提示时按提示输入; 其他时候输入的内容会作为聊天发出。\n",
           name.c_str());
    sendLine(fd, "JOIN " + name);

    std::thread recvThread([fd] {
        std::string buf;
        char tmp[4096];
        while (true) {
            ssize_t n = recv(fd, tmp, (int)sizeof tmp, 0);
            if (n <= 0) {
                printf("\n[与服务器断开连接]\n");
                pauseBeforeExit();
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
    while (std::getline(std::cin, line)) {
        if (!sendLine(fd, line)) break;
    }
    closesock(fd);
    return 0;
}
