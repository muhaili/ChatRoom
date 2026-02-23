#ifndef DB_H
#define DB_H
#include <mysql/mysql.h>
#include <string>
#include <iostream>
#include <cstdio>
#include "protocol.h"
using namespace std;
class db
{
private:
    MYSQL *_conn;

public:
    bool connect();
    bool update_status(int id, string status);
    REGISTER_ANS user_register(string name, string password, int &user_id);
    LOGIN_ANS user_login(int id, string password);
    LOGIN_ANS user_logout(int id);
    void insert_offline_msg(int userid, string msg);
    vector<string> query_offline_msg(int userid);
    void remove_offline_msg(int userid);
};
#endif