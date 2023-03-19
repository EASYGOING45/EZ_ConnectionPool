#pragma once
#include <mysql/mysql.h>
#include <string>
#include <ctime>
using namespace std;

/**
 * 实现MySQL数据库的操作
 */
class Connection
{
public:
    Connection();  // 构造函数 初始化数据库连接
    ~Connection(); // 析构函数 关闭数据库连接

    bool connect(string ip, unsigned short port, string user, string password, string dbname); // 连接数据库
    bool update(string sql);                                                                   // 更新数据库操作 INSERT DELETE UPDATE
    MYSQL_RES *query(string sql);                                                              // 查询数据库操作 SELECT

    // 刷新一下连接池起始的空闲时间点
    void refreshAliveTime() { _alivetime = clock(); }

    // 获取连接的存活时间
    clock_t getAliveTime() const { return _alivetime; }

private:
    MYSQL *_conn;       // 数据库连接的句柄
    clock_t _alivetime; // 数据库连接的存活时间
};