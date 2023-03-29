## config类详解

config类的主要作用是接收main函数的输入，配置相关参数；

成员变量如下：

```c++
- int port;         /* 端口号 */ 
- int logWrite;   /* 日志写入方式 */ 
- int trigMode;  /* 触发组合模式 */
- int listenTrigMode;  /* listenfd 触发模式 */
- int connTrigMode;   /* connfd 触发模式 */
- int opt_linger;    /* 优雅的关闭连接 */
- int sql_num;     /* 数据库连接池数量 */
- int thread_num;    /* 线程池内的线程数量 */
- int close_log;    /* 是否关闭日志 */
- int actor_model;   /* 并发模型选择 */
```

构造函数（设置默认参数）：

```
Config::Config(){
  port = 9006; //端口号,默认9006
  logWrite = 0;  //日志写入方式，默认同步
  trigMode = 0;  //触发组合模式,默认listenfd LT + connfd LT
  listenTrigMode = 0;  //listenfd触发模式，默认LT
  connTrigMode = 0;  //connfd触发模式，默认LT  
  opt_linger = 0;  //优雅关闭链接，默认不使用 
  sql_num = 8;  //数据库连接池数量,默认8
  thread_num = 8;  //线程池内的线程数量,默认8  
  close_log = 0;  //关闭日志,默认不关闭 
  actor_model = 0;  //并发模型,默认是proactor
}
```

成员函数：

```
void parse_arg(int argc, char* argv[]); /* 接收main 函数的输入，将成员变量设置成对应的值 */
如果用户调用 main 函数时有输入指定参数，使用 getopt 获取并修改对应值
```

