# 基于C++11和Linux环境的自写数据库连接池

## 关键技术要点

- MySQL数据库编程
- 设计模式中的单例模式
- STL中的queue队列容器
- C++11多线程编程
- C++11线程互斥、线程同步通信和unique_lock
- 基于CAS的原子整形
- C++11中的智能指针shared_ptr、lambda表达式
- 生产者-消费者线程模型

## 项目背景

为提高MySQL数据库（C/S架构设计）的访问瓶颈，除在服务器层面增加缓存服务器（如Redis）缓存常用的数据之外，还可以增加连接池，来提高MySQL Server的访问效率。

在高并发情况下，大量的TCP三次握手、MySQL连接认证、MySQL Server关闭连接回收资源和TCP四次挥手所耗费的性能时间是很大的，增加连接池可以减少这一部分的性能损耗。

本项目使用C++11技术栈，实现了Linux环境下的数据库连接池。

## 数据库连接池简介

数据库连接池一般包含了数据库连接所使用的`IP地址、Port端口号、用户名和密码以及其它的性能参数`，例如初始连接量，最大连接量，最大空闲时间，连接超时时间等，该项目基于C++实现，实现了大部分数据库连接池都具有的通用基础功能。

- `初始连接量（initSize）`：表示连接池事先会和MySQL Server创建 `initSize`个数的connection连接，当应用发起MySQL访问时，不用再创建和MySQL Server新的连接，直接从连接池中获取一个可用的连接就可以，使用完成后，并不去释放connection，而是把当前connection归还到连接池中，这样就减少了四次挥手和关闭连接回收资源的消耗
- `最大连接量（maxSize）`：当并发访问MySQL Server的请求增多时，初始连接量已经不够使用了，此 时会根据新的请求数量去创建更多的连接给应用去使用，但是新创建的连接数量上限是maxSize，不能 无限制的创建连接，因为每一个连接都会占用一个socket资源，一般连接池和服务器程序是部署在一台 主机上的，如果连接池占用过多的socket资源，那么服务器就不能接收太多的客户端请求了。当这些连 接使用完成后，再次归还到连接池当中来维护。
- `最大空闲时间（maxIdleTime）`：当访问MySQL的并发请求多了以后，连接池里面的连接数量会动态 增加，上限是maxSize个，当这些连接用完再次归还到连接池当中。如果在指定的maxIdleTime里面， 这些新增加的连接都没有被再次使用过，那么新增加的这些连接资源就要被回收掉，只需要保持初始连 接量initSize个连接就可以了。
- `连接超时时间（connectionTimeout）`：当MySQL的并发请求量过大，连接池中的连接数量已经到达 maxSize了，而此时没有空闲的连接可供使用，那么此时应用从连接池获取连接无法成功，它通过阻塞 的方式获取连接的时间如果超过connectionTimeout时间，那么获取连接失败，无法访问数据库。

## MySQL Server参数

```sql
mysql>show variables like 'max_connectons';
```

该命令可以查看MySQL Server所支持的最大连接个数，超过max_connections数量的连接，MySQL Server会直接拒绝，所以在使用连接池增加连接数量的时候，MySQL Server的max_connections参数 也要适当的进行调整，以适配连接池的连接上限。

## 功能实现设计

- ConnectionPool.cpp和ConnectionPool.h：连接池代码实现 

- Connection.cpp和Connection.h：数据库操作代码、增删改查代码实现

数据库连接池主要包含了以下功能点：

1. 连接池只需要一个实例，所以ConnectionPool以单例模式进行设计 
2. 从ConnectionPool中可以获取和MySQL的连接Connection 
3. 空闲连接Connection全部维护在一个线程安全的Connection队列中，使用线程互斥锁保证队列的线 程安全 
4. 如果Connection队列为空，还需要再获取连接，此时需要动态创建连接，上限数量是maxSize 
5. 队列中空闲连接时间超过maxIdleTime的就要被释放掉，只保留初始的initSize个连接就可以了，这个 功能点肯定需要放在独立的线程中去做 
6. 如果Connection队列为空，而此时连接的数量已达上限maxSize，那么等待connectionTimeout时间 如果还获取不到空闲的连接，那么获取连接失败，此处从Connection队列获取空闲连接，可以使用带 超时时间的mutex互斥锁来实现连接超时时间 
7. 用户获取的连接用shared_ptr智能指针来管理，用lambda表达式定制连接释放的功能（不真正释放 连接，而是把连接归还到连接池中） 
8. 连接的生产和连接的消费采用生产者-消费者线程模型来设计，使用了线程间的同步通信机制条件变量 和互斥锁

## 生产者-消费者模型实践

```C++
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

```

### 生产者线程

```C++
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
```

### 消费者线程

```C++
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
            if (p->getAliveTime() >= {_maxIdleTime * 1000})
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
```

### 获取连接

```C++
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
```

