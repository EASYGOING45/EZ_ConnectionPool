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

// 运行在独立的线程中，专门负责生产新连接
void ConnectionPool::produceConnectionTask()
{
    for (;;)
    {
        unique_lock<mutex> lock(_queueMutex); // Lock
        while (!_connectionQue.empty())
        {
            cv.wait(lock); // 队列不空，此时生产线程进入等待状态
        }

        // 连接数量没有到达上限，继续创建新的连接
        if (_connectionCnt < _maxSize)
        {
            Connection *p = new Connection();
            p->connect(_ip, _port, _username, _password, _dbname); // 一条新的连接
            p->refreshAliveTime();                                 // 刷新开始空闲的起始时间
            _connectionQue.push(p);
            _connectionCnt++;
        }

        // 通知消费者线程，可以进行消费了
        cv.notify_all(); // notify_all
    }
}

// 给外部提供接口，从连接池中获取一个可用的空闲连接
shared_ptr<Connection> ConnectionPool::getConnecction()
{
    unique_lock<mutex> lock(_queueMutex);
    while (_connectionQue.empty())
    {
        // sleep
        if (cv_status::timeout == cv.wait_for(lock, chrono::milliseconds(_connectionTimeout)))
        {
            if (_connectionQue.empty())
            {
                LOG("获取空闲连接超时......获取失败!");
                return nullptr;
            }
        }
    }

    /**
     * shared_ptr智能指针析构时，会把connection资源直接delete掉，相当于调用connection的析构函数，connection就被close掉了。
     * 这里需要自定义shared_ptr的释放资源的方式，把connection直接归还到queue当中，而不是delete掉。
     */
    // 从队列中取出一个连接 lambda表达式 作为shared_ptr的释放资源的方式
    shared_ptr<Connection> sp(_connectionQue.front(),
                              [&](Connection *pcon)
                              {
                                  // 此处在服务器应用线程中调用，所以一定要考虑队列的线程安全操作
                                  unique_lock<mutex> lock(_queueMutex);
                                  pcon->refreshAliveTime(); // 刷新一下开始空闲的起始时间
                                  _connectionQue.push(pcon);
                              });
    _connectionQue.pop(); // 从队列中取出一个连接
    cv.notify_all();      // 通知生产者线程，可以进行生产了

    return sp; // 返回一个shared_ptr
}

// 运行在独立的线程中，专门负责扫描超时的空闲连接
void ConnectionPool::scannerConnectionTask()
{
    for (;;)
    {
        // 通过sleep来模拟定时效果
        this_thread::sleep_for(chrono::seconds(_maxIdleTime));

        // 扫描整个队列，释放多余的连接
        unique_lock<mutex> lock(_queueMutex);
        while (_connectionCnt > _initSize)
        {
            Connection *p = _connectionQue.front();
            if (p->getAliveTime() >= (_maxIdleTime * 1000))
            {
                _connectionQue.pop();
                _connectionCnt--;
                delete p; // 调用~Connection()释放连接
            }
            else
            {
                break; // 队头的连接没有超过_maxIdleTime，其他连接肯定也没有
            }
        }
    }
}