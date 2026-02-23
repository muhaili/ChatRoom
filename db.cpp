#include "db.h"
#include "protocol.h"
#include <mysql/mysql.h>
#include <string>
#include <iostream>
#include <cstdio>
#include <cstring>
using namespace std;
bool db::connect()
{
    _conn = mysql_init(NULL);
    if (mysql_real_connect(_conn, "127.0.0.1", "root", "123456", "chat_db", 3306, nullptr, 0))
    {
        mysql_set_character_set(_conn, "utf8");
        return true;
    }
    else
    {
        std::cerr << "连接失败：" << mysql_error(_conn) << std::endl;
        return false;
    }
}
REGISTER_ANS db::user_register(string name, string password, int &user_id)
{
    // 1. 创建预处理句柄
    MYSQL_STMT *stmt = mysql_stmt_init(_conn);
    if (!stmt)
        return REGISTER_ANS::FALSE;

    // 2. 准备 SQL 模板 (使用 ? 作为占位符)
    string sql = "INSERT INTO user(name, password, status) VALUES(?, ?, 'offline')";
    if (mysql_stmt_prepare(stmt, sql.c_str(), sql.length()))
    {
        mysql_stmt_close(stmt);
        return REGISTER_ANS::FALSE;
    }

    // 3. 绑定参数 (MYSQL_BIND 结构体数组)
    MYSQL_BIND bind[2];
    memset(bind, 0, sizeof(bind));

    // 绑定第一个参数：name
    unsigned long name_len = name.length();
    bind[0].buffer_type = MYSQL_TYPE_STRING;
    bind[0].buffer = (char *)name.c_str();
    bind[0].buffer_length = name.length();
    bind[0].length = &name_len;

    // 绑定第二个参数：password
    unsigned long pwd_len = password.length();
    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].buffer = (char *)password.c_str();
    bind[1].buffer_length = password.length();
    bind[1].length = &pwd_len;

    // 将绑定结构应用到句柄
    if (mysql_stmt_bind_param(stmt, bind))
    {
        mysql_stmt_close(stmt);
        return REGISTER_ANS::FALSE;
    }

    // 4. 执行
    if (mysql_stmt_execute(stmt))
    {
        // 如果执行失败，检查是否是因为用户名冲突 (Unique Key)
        unsigned int err_no = mysql_stmt_errno(stmt);
        mysql_stmt_close(stmt);
        if (err_no == 1062)
            return REGISTER_ANS::USER_NAME_EXIST; // 1062 是 MySQL 唯一键冲突错误码
        return REGISTER_ANS::FALSE;
    }

    user_id = (int)mysql_insert_id(_conn);
    // 5. 释放资源
    mysql_stmt_close(stmt);
    return REGISTER_ANS::SUCCESS;
}
LOGIN_ANS db::user_login(int id, string password)
{
    MYSQL_STMT *stmt = mysql_stmt_init(_conn);
    if (!stmt)
        return LOGIN_ANS::FALSE;

    // 2. 准备 SQL 模板
    string sql = "select password from user where id = ?";
    if (mysql_stmt_prepare(stmt, sql.c_str(), sql.length()))
    {
        mysql_stmt_close(stmt);
        return LOGIN_ANS::FALSE;
    }

    // 3. 绑定输入参数 (WHERE id = ?)
    MYSQL_BIND bind_in[1];
    memset(bind_in, 0, sizeof(bind_in));

    bind_in[0].buffer_type = MYSQL_TYPE_LONG; // 对应数据库的 INT
    bind_in[0].buffer = (char *)&id;          // 指向 id 变量
    bind_in[0].is_null = 0;
    bind_in[0].length = 0;

    if (mysql_stmt_bind_param(stmt, bind_in))
    {
        mysql_stmt_close(stmt);
        return LOGIN_ANS::FALSE;
    }

    // 4. 执行查询
    if (mysql_stmt_execute(stmt))
    {
        mysql_stmt_close(stmt);
        return LOGIN_ANS::FALSE;
    }

    // 5. 绑定输出结果 (接收查出来的 password)
    // 即使只查一个字段，也得绑定一次结果集
    MYSQL_BIND bind_out[1];
    memset(bind_out, 0, sizeof(bind_out));

    char res_pwd[64] = {0};        // 准备一个数组接住数据库里的密码
    unsigned long res_pwd_len = 0; // 存密码的实际长度

    bind_out[0].buffer_type = MYSQL_TYPE_STRING;
    bind_out[0].buffer = (char *)res_pwd;
    bind_out[0].buffer_length = sizeof(res_pwd);
    bind_out[0].length = &res_pwd_len;

    if (mysql_stmt_bind_result(stmt, bind_out))
    {
        mysql_stmt_close(stmt);
        return LOGIN_ANS::FALSE;
    }

    // 6. 获取数据 (Fetch)
    int status = mysql_stmt_fetch(stmt);
    if (status == 0)
    {
        // 查到了！开始比对
        // 注意：res_pwd 拿到的是 char 数组，我们需要指定长度转 string 比较
        if (password == string(res_pwd, res_pwd_len))
        {
            mysql_stmt_close(stmt);
            update_status(id, "online");
            return LOGIN_ANS::SUCCESS;
        }
        else
        {
            mysql_stmt_close(stmt);
            return LOGIN_ANS::PASSWORD_ERROR;
        }
    }
    else if (status == MYSQL_NO_DATA)
    {
        // ID 不存在
        mysql_stmt_close(stmt);
        return LOGIN_ANS::USER_NOT_EXIST;
    }

    // 其他错误情况
    mysql_stmt_close(stmt);
    return LOGIN_ANS::FALSE;
}
LOGIN_ANS db::user_logout(int id)
{
    if (update_status(id, "offline"))
    {
        return LOGIN_ANS::SUCCESS;
    }
    else
    {
        return LOGIN_ANS::FALSE;
    }
}
bool db::update_status(int id, string status)
{
    MYSQL_STMT *stmt = mysql_stmt_init(_conn);
    if (!stmt)
        return false;

    // SQL 模板
    string sql = "UPDATE user SET status = ? WHERE id = ?";
    if (mysql_stmt_prepare(stmt, sql.c_str(), sql.length()))
    {
        mysql_stmt_close(stmt);
        return false;
    }

    // 绑定两个参数：status(string) 和 id(int)
    MYSQL_BIND bind[2];
    memset(bind, 0, sizeof(bind));

    // 参数 1: status
    unsigned long status_len = status.length();
    bind[0].buffer_type = MYSQL_TYPE_STRING;
    bind[0].buffer = (char *)status.c_str();
    bind[0].buffer_length = status.length();
    bind[0].length = &status_len;

    // 参数 2: id
    bind[1].buffer_type = MYSQL_TYPE_LONG;
    bind[1].buffer = (char *)&id;

    if (mysql_stmt_bind_param(stmt, bind))
    {
        mysql_stmt_close(stmt);
        return false;
    }

    if (mysql_stmt_execute(stmt))
    {
        mysql_stmt_close(stmt);
        return false;
    }

    mysql_stmt_close(stmt);
    return true;
}

void db::insert_offline_msg(int userid, string msg)
{

    std::vector<char> escape_msg(msg.length() * 2 + 1);

    // 执行转义，防止消息中的引号破坏 SQL 语句
    mysql_real_escape_string(_conn, escape_msg.data(), msg.c_str(), msg.length());

    // 拼接 SQL (2048 基本够用，如果消息极长可以动态计算 sql_buffer 大小)
    char sql[2048] = {0};
    sprintf(sql, "INSERT INTO offline_message (userid, message) VALUES(%d, '%s')",
            userid, escape_msg.data());

    if (mysql_query(_conn, sql))
    {
        std::cerr << "Insert offline message error: " << mysql_error(_conn) << std::endl;
    }
}

// 2. 查询离线消息
vector<string> db::query_offline_msg(int userid)
{
    char sql[1024] = {0};
    sprintf(sql, "SELECT message FROM offline_message WHERE userid = %d", userid);

    vector<string> vec;
    if (mysql_query(_conn, sql) == 0)
    {
        MYSQL_RES *res = mysql_use_result(_conn); // 或者用 mysql_store_result
        if (res != nullptr)
        {
            MYSQL_ROW row;
            // 将该用户所有的离线消息全部存入 vector
            while ((row = mysql_fetch_row(res)) != nullptr)
            {
                vec.push_back(row[0]);
            }
            mysql_free_result(res);
        }
    }
    return vec;
}

// 3. 删除离线消息 (通常在推送完之后调用)
void db::remove_offline_msg(int userid)
{
    char sql[1024] = {0};
    sprintf(sql, "DELETE FROM offline_message WHERE userid = %d", userid);

    if (mysql_query(_conn, sql))
    {
        std::cerr << "Remove offline message error: " << mysql_error(_conn) << std::endl;
    }
}