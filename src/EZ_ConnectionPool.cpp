#include "EZ_ConnectionPool.h"
#include "public.h"

// 线程安全的懒汉单例函数接口
ConnectionPool *ConnectionPool::getConnectionPool()
{
    static ConnectionPool connPool; // 静态局部变量，线程安全
    return &connPool;               // 返回单例对象的地址
}

// 从配置文件中加载配置项
bool ConnectionPool::loadConfigFile()
{
    FILE *pf = fopen("mysql.ini", "r");
    if (pf == nullptr)
    {
        LOG("mysql.ini file is not exist!");
        return false;
    }

    // peof()函数用于判断文件是否读取完毕
    while (!feof(pf))
    {
        char line[1024] = {0}; // 存放每一行的配置项
        fgets(line, 1024, pf); // 读取每一行的配置项
        string str = line;
        int idx = str.find('=', 0); // 查找等号的位置
        if (idx == -1)              // 无效的配置项
        {
            continue;
        }

        // password=123456\n
        int endidx = str.find('\n', idx);                     // 查找换行符的位置
        string key = str.substr(0, idx);                      // 截取等号前面的字符串
        string value = str.substr(idx + 1, endidx - idx - 1); // 截取等号后面的字符串

        if (key == "ip")
        {
            _ip = value;
        }
        else if (key == "port")
        {
            _port = atoi(value.c_str());
        }
        else if (key == "username")
        {
            _username = value;
        }
        else if (key == "password")
        {
            _password = value;
        }
        else if (key == "dbname")
        {
            _dbname = value;
        }
        else if (key == "initSize")
        {
            _initSize = atoi(value.c_str());
        }
        else if (key == "maxSize")
        {
            _maxSize = atoi(value.c_str());
        }
        else if (key == "maxIdleTime")
        {
            _maxIdleTime = atoi(value.c_str());
        }
        else if (key == "connectionTimeOut")
        {
            _connectionTimeout = atoi(value.c_str());
        }
    }
    return true;
}

// 连接池的构造
ConnectionPool::ConnectionPool()
{
    // 是否已经加载配置项
    if (!loadConfigFile())
    {
        return;
    }

    // 创建初始数量的连接
    for (int i = 0; i < _initSize; ++i)
    {
        Connection *p = new Connection();
        p->connect(_ip, _port, _username, _password, _dbname);
        p->refreshAliveTime(); // 刷新一下开始空闲的起始时间
        _connectionQue.push(p);
        _connectionCnt++;
    }

    // 启动一个新的线程，作为数据库连接的生产者 linux thread => pthread_create
    thread produce(std::bind(&ConnectionPool::produceConnectionTask, this));
    produce.detach();

    // 启动一个新的定时线程，扫描超过maxIdleTime时间的空闲连接，进行对应的连接回收
    thread scanner(std::bind(&ConnectionPool::scannerConnectionTask, this));
    scanner.detach();
}