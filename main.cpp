#include "server.h"
#include "db.h"
#include <iostream>
#include <signal.h>
#include <iostream>
#include <muduo/net/EventLoop.h>

using namespace muduo;
using namespace muduo::net;

// 处理服务器 Ctrl+C 退出，确保退出时数据库连接正常（可选）
void handle_sigint(int sig)
{
    std::cout << "\nServer is shutting down..." << std::endl;
    exit(0);
}

int main()
{
    // 1. 获取业务层单例并初始化数据库
    // 假设你的 ChatService 中有一个方法获取 db 引用，或者在 ChatService 初始化时连数据库
    if (!ChatService::instance()->connectDatabase())
    {
        std::cerr << "致命错误：无法连接到 MySQL 数据库！" << std::endl;
        std::cerr << "请检查：1. MySQL 服务是否启动；2. 用户名密码是否正确；3. 是否创建了 chat_db 库。" << std::endl;
        return -1;
    }
    std::cout << ">>> 数据库连接成功 <<<" << std::endl;
    ChatService::instance()->init();
    // 2. 初始化网络模块
    EventLoop loop;
    InetAddress addr("127.0.0.1", 6000);
    ChatServer server(&loop, addr);

    // 3. 启动
    server.start();
    std::cout << ">>> ChatServer started at 127.0.0.1:6000 <<<" << std::endl;

    loop.loop();

    return 0;
}