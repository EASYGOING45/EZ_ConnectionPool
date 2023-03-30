#include "public.h"
#include "Connection.h"
#include <iostream>
using namespace std;

Connection::Connection()
{
    // 初始化数据库连接
    _conn = mysql_init(nullptr);
}

Connection::~Connection()
{
    // 关闭数据库连接
    mysql_close(_conn);
}

// 连接数据库
bool Connection::connect(string ip, unsigned short port,
                         string username, string password, string dbname)
{
    // 连接数据库
    MYSQL *p = mysql_real_connect(_conn, ip.c_str(), username.c_str(),
                                  password.c_str(), dbname.c_str(), port, nullptr, 0);
    return p != nullptr;
}

// 执行sql语句
bool Connection::update(string sql)
{
    // 更新操作 INSERT DELETE UPDATE
    if (mysql_query(_conn, sql.c_str()) != 0)
    {
        LOG("update error!" + sql);
        return false;
    }
    return true;
}

MYSQL_RES *Connection::query(string sql)
{
    // 查询操作 SELECT
    if (mysql_query(_conn, sql.c_str()) != 0)
    {
        LOG("query error!" + sql);
        return nullptr;
    }
    return mysql_store_result(_conn);
}