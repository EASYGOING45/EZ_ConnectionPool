#pragma once
// 数据库连接池
#include <string>
#include <queue>
#include <mutex>
#include <iostream>
#include <atomic>
#include <thread>
#include <condition_variable> //该头文件中包含了condition_variable类 用于实现线程间的同步
#include <memory>
#include <functional>
using namespace std;

#include "Connection.h"

/**
 * 实现连接池功能模块
 * class ConnectionPool
 */
class ConnectionPool
{
public:
    // 获取连接池对象实例
    static ConnectionPool *getConnectionPool();

    // 给外部提供接口，从连接池中获取一个可用的空闲连接
    shared_ptr<Connection> getConnecction();

private:
    // 单例模式 Note1、构造函数私有化
    ConnectionPool();

    // 从配置文件中加载配置项
    bool loadConfigFile();

    // 生产者-消费者模式
    // 运行在独立的线程中，专门负责生产新连接
    void produceConnectionTask();

    // 扫描超过maxIDleTime时间的空闲连接，进行回收
    void scannerConnectionTask();

    string _ip;           // mysql的ip地址
    unsigned short _port; // 端口号 默认3306
    string _username;     // mysql登录用户名
    string _password;     // mysql登录密码
    string _dbname;       // 连接的数据库名称

    int _initSize;          // 连接池的初始连接量
    int _maxSize;           // 连接池的最大连接量
    int _maxIdleTime;       // 连接池最大空闲时间
    int _connectionTimeout; // 连接池获取连接的超时时间

    queue<Connection *> _connectionQue; // 存储mysql连接的队列
    mutex _queueMutex;                  // 维护连接队列的线程安全互斥锁
    atomic_int _connectionCnt;          // 原子操作 记录连接所创建的connection连接的总数量
    condition_variable cv;              // 条件变量，用于连接成产线程和连接消费线程的通信操作
};