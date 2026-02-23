#include "server.h"
#include <muduo/net/TcpServer.h>     // 核心：服务器类
#include <muduo/net/EventLoop.h>     // 核心：事件循环（死循环）
#include <muduo/net/TcpConnection.h> // 核心：连接对象指针
#include <muduo/base/Timestamp.h>    // 辅助：打印消息时间戳
#include <nlohmann/json.hpp>
#include <functional>
#include <map>
#include <mutex>
#include <iostream>
using namespace std;
using namespace muduo::net;
using json = nlohmann::json;
using namespace std::placeholders;
using namespace muduo;
ChatServer::ChatServer(EventLoop *loop, const InetAddress &listenAddr)
    : _server(loop, listenAddr, "ChatServer"),
      _loop(loop)
{
    _server.setConnectionCallback(bind(&ChatServer::onConnection, this, placeholders::_1));
    _server.setMessageCallback(bind(&ChatServer::onMessage, this,
                                    placeholders::_1, placeholders::_2, placeholders::_3));

    _server.setThreadNum(4);
}
void ChatServer::onConnection(const TcpConnectionPtr &conn)
{
    if (conn->disconnected())
    {
        ChatService::instance()->user_closeConnection(0, conn, Timestamp::now());
    }
}
void ChatServer::onMessage(const TcpConnectionPtr &conn, Buffer *buf, Timestamp time)
{
    printf("收到数据，长度: %ld\n", buf->readableBytes());
    // 打印前 8 个字节的十六进制
    const char *data = buf->peek();
    for (int i = 0; i < 8 && i < buf->readableBytes(); ++i)
    {
        printf("%02x ", (unsigned char)data[i]);
    }
    printf("\n");
    while (buf->readableBytes() >= sizeof(Header))
    {
        Header *header = (Header *)buf->peek();
        cout << (int)header->type << endl;
        uint32_t len = header->msglen;
        if (buf->readableBytes() < sizeof(Header) + len)
        {
            cout << "不合法" << endl;
            return;
        }
        buf->retrieve(sizeof(Header));
        string jsonStr = buf->retrieveAsString(len);
        cout << jsonStr << endl;
        try
        {
            nlohmann::json js = nlohmann::json::parse(jsonStr);
            auto handler = ChatService::instance()->getHandler(header->type);
            handler(js, conn, Timestamp::now());
        }
        catch (...)
        {
            conn->shutdown(); // JSON非法，踢掉
        }
    }
}
void ChatServer::start()
{
    _server.start();
}

ChatService *ChatService::instance()
{
    static ChatService service;
    return &service;
}
bool ChatService::connectDatabase()
{
    return database.connect(); // _db 是你的 db 类成员
}
void ChatService::init()
{
    // 使用 bind 直接绑定成员函数
    _handleMap[msgType::REGISTER] = std::bind(&ChatService::user_register, this, _1, _2, _3);
    _handleMap[msgType::LOGIN] = std::bind(&ChatService::user_login, this, _1, _2, _3);
    _handleMap[msgType::LOG_OUT] = std::bind(&ChatService::user_logout, this, _1, _2, _3);
    _handleMap[msgType::CHAT_ONE] = std::bind(&ChatService::user_chatone, this, _1, _2, _3);
    _handleMap[msgType::CHAT_ALL] = std::bind(&ChatService::user_chatall, this, _1, _2, _3);
}
function<void(const json &, TcpConnectionPtr, Timestamp)> ChatService::getHandler(msgType type)
{
    auto it = _handleMap.find(type);
    if (it == _handleMap.end())
    {
        cout << "未知类型" << endl;
        return [=](const nlohmann::json &, muduo::net::TcpConnectionPtr, muduo::Timestamp)
        {
            // 这里可以记录日志：找不到对应的处理器
        };
    }
    return it->second;
}
string ChatService::pack(Header header, string body)
{
    string package;
    package.append((char *)&header, sizeof(Header));
    package.append(body);
    return package;
}
void ChatService::user_register(const json &js, TcpConnectionPtr conn, Timestamp time)
{
    string name = js["name"];
    string pwd = js["password"];

    int user_id = -1;
    std::cout << "DEBUG: 进入了 user_register 处理函数！" << std::endl;
    REGISTER_ANS ans = database.user_register(name, pwd, user_id);
    cout << user_id << endl;
    nlohmann::json response;
    response["msgid"] = (uint8_t)msgType::REGISTER_ANS;
    if (ans == REGISTER_ANS::SUCCESS)
    {
        response["id"] = user_id;
    }

    string body = response.dump();
    Header header;
    header.type = msgType::REGISTER_ANS;
    header.res = (uint8_t)ans;
    header.msglen = body.size();
    string package = pack(header, body);
    conn->send(package);
}
void ChatService::user_login(const json &js, TcpConnectionPtr conn, Timestamp time)
{
    int id = js["id"];
    string pwd = js["password"];
    LOGIN_ANS ans = database.user_login(id, pwd);

    nlohmann::json response;
    response["msgid"] = (uint8_t)msgType::LOGIN_ANS;

    if (ans == LOGIN_ANS::SUCCESS)
    {
        {
            unique_lock<mutex> lock(_connMutex);
            _userConn[id] = conn;
            conn->setContext(id); // 存一个双向的，意外掉线的时候拿不到id的情况下，还能通过连接找回
        }
        // 1. 更新数据库状态（你之前的代码漏了这一行，logout里有，login也得有）
        database.update_status(id, "online");

        // 2. 拉取并推送离线消息
        vector<string> vec = database.query_offline_msg(id);
        if (!vec.empty())
        {
            for (const string &msgStr : vec)
            {
                Header h = {msgType::CHAT_ONE, 1, (uint32_t)msgStr.size(), 0};
                conn->send(pack(h, msgStr));
            }
            // 3. 推完即删
            database.remove_offline_msg(id);
        }
    }
    string body = response.dump();
    Header header;
    header.type = msgType::LOGIN_ANS;
    header.res = (uint8_t)ans;
    header.msglen = body.size();
    header.padding = 0;

    conn->send(pack(header, body));
}
void ChatService::user_logout(const json &js, TcpConnectionPtr conn, Timestamp time)
{
    int id = js["id"];
    {
        std::lock_guard<std::mutex> lock(_connMutex);
        auto it = _userConn.find(id);
        if (it != _userConn.end() && it->second == conn)
        {
            _userConn.erase(it);
            database.update_status(id, "offline");
        }
    }
    Header header;
    header.type = msgType::LOG_OUT;
    header.res = (uint8_t)LOGIN_ANS::SUCCESS; // 成功
    header.msglen = 0;
    header.padding = 0;
    conn->send(pack(header, ""));
}
void ChatService::user_chatone(const json &js, TcpConnectionPtr conn, Timestamp time)
{
    int toId = js["toid"];
    string msg = js["msg"];
    int fromId = boost::any_cast<int>(conn->getContext());

    nlohmann::json forwardJs;
    forwardJs["msgid"] = (uint8_t)msgType::CHAT_ONE;
    forwardJs["fromid"] = fromId;
    forwardJs["msg"] = msg;
    forwardJs["time"] = time.toFormattedString();

    TcpConnectionPtr toConn = nullptr;
    {
        lock_guard<mutex> lock(_connMutex);
        auto it = _userConn.find(toId);
        if (it != _userConn.end())
            toConn = it->second;
    }

    if (toConn != nullptr)
    {
        string body = forwardJs.dump();
        Header header = {msgType::CHAT_ONE, 1, (uint32_t)body.size(), 0};
        toConn->send(pack(header, body));
    }
    else
    {
        database.insert_offline_msg(toId, forwardJs.dump());
    }
}
void ChatService::user_chatall(const json &js, TcpConnectionPtr conn, Timestamp time)
{
    int fromId = boost::any_cast<int>(conn->getContext());
    string msg = js["msg"];

    nlohmann::json forwardJs;
    forwardJs["msgid"] = (uint8_t)msgType::CHAT_ALL;
    forwardJs["fromid"] = fromId;
    forwardJs["msg"] = msg;
    forwardJs["time"] = time.toFormattedString();

    string body = forwardJs.dump();
    Header header = {msgType::CHAT_ALL, 1, (uint32_t)body.size(), 0};
    string package = pack(header, body);

    // 锁定在线连接表，进行广播
    {
        lock_guard<mutex> lock(_connMutex);
        for (auto it = _userConn.begin(); it != _userConn.end(); ++it)
        {
            // 排除发送者本人
            if (it->first != fromId)
            {
                it->second->send(package);
            }
        }
    }
}
void ChatService::user_closeConnection(const json &js, TcpConnectionPtr conn, Timestamp time)
{
    if (!conn->getContext().empty()) // 如果注册完了断联不做处理，因为是登录后才算在线用户，才改数据库状态+填进在线用户的表，
    // 先检查下有没有登出处理，登陆后没显示调用登出就断联的话，就需要我们这边做一下清理了，否则的话也不需要做啥处理
    { // 由于已经拜拜了，我们也不需要再回复数据包了
        int id = boost::any_cast<int>(conn->getContext());
        {
            std::lock_guard<std::mutex> lock(_connMutex);
            _userConn.erase(id);
        }
        database.update_status(id, "offline");
    }
}
