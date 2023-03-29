#ifndef CONFIG_H
#define CONFIG_H

#include "../WebServer/WebServer.h"

using namespace std;

class Config {
public:
    Config();
    ~Config() {}

    void parse_arg(int argc, char* argv[]);

    int port; /* 端口号 */
    int logWrite;  /* 日志写入方式 */
    int trigMode;  /* 触发组合模式 */
    int listenTrigMode;  /* listenfd 触发模式 */
    int connTrigMode;    /* connfd 触发模式 */
    int opt_linger;       /* 优雅的关闭连接 */
    int sql_num;         /* 数据库连接池数量 */
    int thread_num;      /* 线程池内的线程数量 */
    int close_log;       /* 是否关闭日志 */
    int actor_model;     /* 并发模型选择 */
};

#endif