## Log日志类详解

本项目日志系统的实现有同步实现和异步实现两种方式，

![流程图](D:\cpp_learning\myTinyWebserver\log\流程图.png)

### 基于阻塞队列的生产者消费者模型

![生产者消费者模型](D:\cpp_learning\myTinyWebserver\log\生产者消费者模型.png)

```
生产者消费者模式为并发编程中的经典模型。以多线程为例，为了实现线程间数据同步，生产者线程与消费者线程共享一个缓冲区，其中生产者线程往缓冲区中push消息，消费者线程从缓冲区中pop消息。生产者消费者模式是程序设计中非常常见的一种设计模式，被广泛运用在解耦、消息队列等场景。具有将生产者、消费者解耦，支持异步等优点。

- 如果生产者处理速度很快，而消费者处理速度很慢，那么生产者就必须等待消费者处理完，才能继续生产数据。
- 如果消费者的处理能力大于生产者，那么消费者就必须等待生产者。为了解决这个问题于是引入了生产者和消费者模式。
- 我的理解是类似于小时候生病去医院吊水那个调节阀控制的小容器的作用！
```

### 阻塞队列的实现

```c++
template <typename T>
class block_queue {
public:
    block_queue(int max_size);
    ~block_queue();
    void clear(); /* 清空阻塞队列 */
    bool full();  /* 判断阻塞队列是否满了 */
    bool empty(); /* 判断阻塞队列是否为空 */
    bool front(T& value);  /* 返回队首元素 */
    bool back(T& value);   /* 返回队尾元素 */
    int size();  /* 返回队列的当前长度 */
    int max_size(); /* 返回队列的最大长度 */
    bool push(const T& item); /* 向队列尾部添加元素 */
    bool pop(T& item); /* 弹出队首元素 */
    bool pop(T& item, int ms_timeout); /* 重载一个处理超时的版本 */
private:
    locker m_mutex;
    cond m_cond;

    T* m_array;
    int m_size;
    int m_max_size;
    int m_front;
    int m_back;
};
```

```
由于阻塞队列需要频繁的插入新的写日志任务，和删除已经写完的任务，所以我们使用循环数组来实现阻塞队列，可以大幅度降低数组元素的移动。

阻塞队列支持的操作如上成员函数所示。

线程从阻塞队列中取任务需要上锁，同时使用条件变量来通知线程取任务。
```

### 日志类

```
- 使用单例模式来保证只有一个日志类实例，进而不同线程写的也都是同一个日志文件，单例模式作为最常用的设计模式之一，保证一个类仅有一个实例，并提供一个访问它的全局访问点，该实例被所有程序模块共享。
- 实现思路：私有化它的构造函数，以防止外界创建单例类的对象；使用类的私有静态指针变量指向类的唯一实例，并用一个公有的静态方法获取该实例。
- 单例模式有两种实现方法，分别是懒汉和饿汉模式。顾名思义，懒汉模式，即非常懒，不用的时候不去初始化，所以在第一次被使用时才进行初始化；饿汉模式，即迫不及待，在程序运行时立即初始化。
```

```
在主线程执行流程的第四步时，主线程调用WebServer::log_write()函数来初始化日志系统。初始化，日志文件大小，最大行数，根据输入的file_name和当前时间来给日志文件命名，然后使用 fopen(log_full_name,"a") 打开文件等待其他程序使用日志向日志文件写日志

如果是异步方式的话，创建一个写线程 一直去执行 flush_log_thread 函数。flush_log_thread 函数内执行 async_write_log函数，
async_write_log函数：
	调用阻塞队列的pop函数取出一行日志，加锁使用fputs写入日志文件。
pop函数：
	如果当前阻塞队列的大小为0， 调用m_cond.wait()挂起，等待被唤醒，否则取出队头元素返回
	
如果是同步的话，日志写入函数与调用写日志函数的线程串行执行，即什么都不用做，等待有线程（可能是主线程，也可能是工作线程，谁调用谁写）调用一下四个宏函数写日志即可：
	//这四个宏定义在其他文件中使用，主要用于不同类型的日志输出
    #define LOG_DEBUG(format, ...) Log::get_instance()->write_log(0, format, __VA_ARGS__)
    #define LOG_INFO(format, ...) Log::get_instance()->write_log(1, format, __VA_ARGS__)
    #define LOG_WARN(format, ...) Log::get_instance()->write_log(2, format, __VA_ARGS__)
    #define LOG_ERROR(format, ...) Log::get_instance()->write_log(3, format, __VA_ARGS__)
```

```
以LOG_INFO("%s","...")为例，
	例如在eventLoop函数中，接收到一个超时送来的信号，管道sock调用dealwithsignal函数将timeout置1，此时主线程调用以下函数：
	LOG_INFO("%s", "timer tick")，由以上宏函数可知调用唯一实例的write_log函数。
	write_log函数内:
		先添加 [INFO]: 这个头部， 上锁，将日志行数加1，判断是否需要分文件，解锁。将可变长参数提取出来，记录当前时间等信息，处		理好要写入日志文件的内容之后判断是同步写入，还是异步写入。
			如果是同步写入的话，上锁，调用LOG_INFO函数的线程调用fputs直接写入日志文件，解锁。
			如果是异步写入的话，使用阻塞队列的push函数将这行内容写入阻塞队列。
			push函数内：如果阻塞队列满了，调用m_cond.broadcast唤醒那个写线程来处理写日志任务，并返回假，否则插入队尾同时也调			  用m_broadcast()函数来唤醒写线程
```

```
条件变量详解：
- 条件变量提供了一种线程间的通知机制，当某个共享数据达到某个值时,唤醒等待这个共享数据的线程。
- 基础 API
	pthread_cond_init函数，用于初始化条件变量
	pthread_cond_destory函数，用于销毁条件变量
	pthread_cond_broadcast函数，以广播的方式唤醒所有等待目标条件变量的线程
	pthread_cond_wait函数，用于等待目标条件变量。该函数调用时需要传入 mutex参数(加锁的互斥锁) ，函数执行时，先把调用线程放入	 条件变量的请求队列，然后将互斥锁mutex解锁，当函数成功返回为0时，表示重新抢到了互斥锁，互斥锁会再次被锁上， 也就是说函数内部	  会有一次解锁和加锁操作.
		- 将线程放在条件变量的请求队列后，内部解锁
		- 线程等待被pthread_cond_broadcast信号唤醒或者pthread_cond_signal信号唤醒，唤醒后去竞争锁
		- 若竞争到互斥锁，内部再次加锁
```

