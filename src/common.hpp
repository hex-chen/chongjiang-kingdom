// 冲奖王国 —— 公共网络工具 (macOS / Linux / Windows)
#pragma once
#include <string>
#include <cstring>

#ifdef _WIN32
  #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
  #endif
  #ifndef NOMINMAX
  #define NOMINMAX
  #endif
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <windows.h>
  #pragma comment(lib, "ws2_32.lib")
  #ifdef _MSC_VER
    typedef long long ssize_t;
  #endif
  typedef SOCKET sock_t;
  static const sock_t BAD_SOCK = INVALID_SOCKET;
  inline bool sockOk(sock_t s) { return s != BAD_SOCK; }
  inline void closesock(sock_t s) { closesocket(s); }
  // 初始化 Winsock, 并让控制台支持 UTF-8 与 ANSI 颜色
  inline void netInit() {
      WSADATA w;
      WSAStartup(MAKEWORD(2, 2), &w);
      SetConsoleOutputCP(65001);
      SetConsoleCP(65001);
      HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
      DWORD m = 0;
      if (GetConsoleMode(h, &m))
          SetConsoleMode(h, m | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
  }
#else
  #include <unistd.h>
  #include <sys/socket.h>
  #include <sys/select.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  typedef int sock_t;
  static const sock_t BAD_SOCK = -1;
  inline bool sockOk(sock_t s) { return s >= 0; }
  inline void closesock(sock_t s) { close(s); }
  inline void netInit() {}
#endif

// 发送一行文本（自动补 \n），失败返回 false
inline bool sendLine(sock_t fd, const std::string &s) {
    std::string t = s + "\n";
    size_t off = 0;
    while (off < t.size()) {
        ssize_t n = ::send(fd, t.data() + off, (int)(t.size() - off), 0);
        if (n <= 0) return false;
        off += (size_t)n;
    }
    return true;
}
