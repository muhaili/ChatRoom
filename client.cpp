#include <iostream>
#include <thread>
#include <string>
#include <vector>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <nlohmann/json.hpp>
#include "protocol.h" // 必须引用这个，确保 Header 结构体一致

using namespace std;
using json = nlohmann::json;

// 接收线程：负责处理服务器主动推送的消息（如私聊、群聊、离线消息）
void readTask(int clientfd)
{
    while (true)
    {
        Header header;
        // 1. 先读 8 字节的 Header
        int len = recv(clientfd, &header, sizeof(Header), 0);
        if (len <= 0)
        {
            cerr << "\r服务器断开连接，请按 Ctrl+C 退出" << endl;
            break;
        }

        // 2. 根据 Header 里的长度读 Body (JSON)
        vector<char> buffer(header.msglen);
        len = recv(clientfd, buffer.data(), header.msglen, 0);
        if (len <= 0)
            break;

        string body(buffer.begin(), buffer.end());
        try
        {
            json js = json::parse(body);
            // 处理不同类型的消息
            if (header.type == msgType::CHAT_ONE)
            {
                cout << "\r[" << js["time"] << "] 用户 " << js["fromid"] << " 对你说: " << js["msg"] << endl;
            }
            else if (header.type == msgType::CHAT_ALL)
            {
                cout << "\r[" << js["time"] << "] 群消息 (来自 " << js["fromid"] << "): " << js["msg"] << endl;
            }
            else if (header.type == msgType::LOGIN_ANS)
            {
                cout << "\r登录结果回执: " << (header.res == (uint8_t)LOGIN_ANS::SUCCESS ? "成功" : "失败") << endl;
                if (header.res == (uint8_t)LOGIN_ANS::PASSWORD_ERROR)
                {
                    cout << "错误原因：密码错误" << endl;
                }
                if (header.res == (uint8_t)LOGIN_ANS::USER_NOT_EXIST)
                {
                    cout << "错误原因：用户不存在" << endl;
                }
            }
            else if (header.type == msgType::REGISTER_ANS)
            {
                cout << "\r注册结果回执: " << (header.res == (uint8_t)REGISTER_ANS::SUCCESS ? "成功" : "失败");
                if (header.res == (uint8_t)REGISTER_ANS::USER_NAME_EXIST)
                {
                    cout << "错误原因：用户名已存在" << endl;
                }
                cout << " 分配ID: " << js.value("id", -1) << endl;
            }
            cout << "command >> " << flush; // 保持提示符
        }
        catch (...)
        {
            cerr << "解析 JSON 失败" << endl;
        }
    }
}

// 辅助函数：打包并发送
void sendPackage(int fd, msgType type, json &js)
{
    string body = js.dump();
    Header header = {type, 0, (uint32_t)body.size(), 0};

    send(fd, &header, sizeof(Header), 0);
    send(fd, body.c_str(), body.size(), 0);
}
#include <vector>
#include <sstream>

// 将字符串 s 按照分隔符 c 分割成数组
vector<string> split(const string &s, char c)
{
    vector<string> v;
    stringstream ss(s);
    string item;
    while (getline(ss, item, c))
    {
        if (!item.empty())
            v.push_back(item);
    }
    return v;
}
int main()
{
    int clientfd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(6000);
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (connect(clientfd, (sockaddr *)&server_addr, sizeof(server_addr)) == -1)
    {
        cerr << "连接服务器失败!" << endl;
        return -1;
    }

    // 开启接收线程
    thread(readTask, clientfd).detach();

    cout << "命令格式: \n1. reg:name:pwd \n2. login:id:pwd \n3. chat:toid:msg \n4. chatall:msg\n";

    while (true)
    {
        cout << "command >> " << flush;
        string line;
        getline(cin, line);
        if (line == "exit" || line.empty())
            break;

        // 使用冒号分割字符串
        vector<string> tokens = split(line, ':');
        if (tokens.empty())
            continue;

        string cmd = tokens[0]; // 获取命令头 (reg, login, etc.)

        if (cmd == "reg" && tokens.size() == 3)
        {
            json js;
            js["name"] = tokens[1];
            js["password"] = tokens[2];
            sendPackage(clientfd, msgType::REGISTER, js);
        }
        else if (cmd == "login" && tokens.size() == 3)
        {
            json js;
            js["id"] = stoi(tokens[1]); // string 转 int
            js["password"] = tokens[2];
            sendPackage(clientfd, msgType::LOGIN, js);
        }
        else if (cmd == "chat" && tokens.size() == 3)
        {
            json js;
            js["toid"] = stoi(tokens[1]);
            js["msg"] = tokens[2];
            // 顺便带上发送者 ID（如果你的业务需要）
            // js["fromid"] = g_currentUser.id;
            sendPackage(clientfd, msgType::CHAT_ONE, js);
        }
        else if (cmd == "chatall" && tokens.size() == 2)
        {
            json js;
            js["msg"] = tokens[1];
            sendPackage(clientfd, msgType::CHAT_ALL, js);
        }
        else
        {
            cout << "[错误] 无效的命令格式！" << endl;
        }
    }
    close(clientfd);
    return 0;
}