#ifndef PROTOCOL_H
#define PROTOCOL_H
#include <cstdint> // 提供 uint8_t, uint32_t 等确切宽度的类型
#include <vector>  // 如果你需要处理动态长度的包
#include <string>  // 处理用户名等字符串
using namespace std;
#pragma pack(push, 1) // 用于保持内存紧凑防止加字节导致的错位

// 本文件用于声明数据包类型
enum class msgType : uint8_t
{
    REGISTER = 1,
    REGISTER_ANS = 2,
    LOGIN = 3,
    LOGIN_ANS = 4,
    LOG_OUT = 5,
    CHAT_ONE = 6,
    CHAT_ALL = 7
};
enum class REGISTER_ANS : uint8_t
{
    SUCCESS = 1,
    USER_NAME_EXIST = 2, // 用户名已存在
    FALSE = 3
};
enum class LOGIN_ANS : uint8_t
{
    SUCCESS = 1,
    PASSWORD_ERROR = 2,
    USER_NOT_EXIST = 3,
    FALSE = 4
};
struct Header
{
    msgType type;
    uint8_t res;
    uint32_t msglen;
    uint16_t padding;
};
#pragma pack(pop)
#endif