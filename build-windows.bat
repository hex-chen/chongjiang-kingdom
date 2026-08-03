@echo off
rem 冲奖王国 Windows 编译脚本 (需要 MinGW-w64 的 g++, 推荐 https://winlibs.com 或 MSYS2)
where g++ >nul 2>nul
if errorlevel 1 (
    echo [错误] 未找到 g++。请安装 MinGW-w64: https://winlibs.com 或 MSYS2, 并把 bin 目录加入 PATH。
    echo 或者用 Visual Studio 命令行: cl /std:c++17 /EHsc /utf-8 /O2 src\server.cpp /Fe:server.exe
    exit /b 1
)
g++ -std=c++17 -O2 -static -o server.exe src\server.cpp -lws2_32
if errorlevel 1 exit /b 1
g++ -std=c++17 -O2 -static -o client.exe src\client.cpp -lws2_32
if errorlevel 1 exit /b 1
echo 编译完成: server.exe / client.exe
