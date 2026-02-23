#ifndef SERVER_H
#define SERVER_H
#include "protocol.h"
#include "db.h"
#include <unordered_map>
#include <functional>
#include <mutex>
#include <string>
#include <map>
#include <functional>
#include <muduo/net/TcpServer.h>     // 核心：服务器类
#include <muduo/net/EventLoop.h>     // 核心：事件循环（死循环）
#include <muduo/net/TcpConnection.h> // 核心：连接对象指针
#include <muduo/base/Timestamp.h>    // 辅助：打印消息时间戳
#include <nlohmann/json.hpp>
using namespace std;
using namespace muduo::net;
using json = nlohmann::json;
using namespace std::placeholders;
using namespace muduo;
// server类负责接收连接+解析消息
class ChatServer
{
    TcpServer _server;
    EventLoop *_loop;

public:
    ChatServer(EventLoop *loop, const InetAddress &listenAddr);
    void start();
    void onConnection(const TcpConnectionPtr &conn);
    void onMessage(const TcpConnectionPtr &conn, Buffer *buf, Timestamp time);
};
// service类负责处理需求
class ChatService
{
public:
    map<int, TcpConnectionPtr> _userConn;
    map<msgType, function<void(const json &, TcpConnectionPtr, Timestamp)>> _handleMap{};
    db database;
    mutex _connMutex;

public:
    static ChatService *instance();
    bool connectDatabase();
    void init();
    function<void(const json &, TcpConnectionPtr, Timestamp)> getHandler(msgType type);
    string pack(Header header, string body);
    void user_register(const json &js, TcpConnectionPtr _conn, Timestamp time);
    void user_login(const json &js, TcpConnectionPtr _conn, Timestamp time);
    void user_logout(const json &js, TcpConnectionPtr _conn, Timestamp time);
    void user_chatone(const json &js, TcpConnectionPtr _conn, Timestamp time);
    void user_chatall(const json &js, TcpConnectionPtr _conn, Timestamp time);
    void user_closeConnection(const json &js, TcpConnectionPtr _conn, Timestamp time);
};
#endif