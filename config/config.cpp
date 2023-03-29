#include "config.h"

Config::Config(){
    
    port = 9006; //端口号,默认9006
    logWrite = 0;  //日志写入方式，默认同步
    trigMode = 0;  //触发组合模式,默认listenfd LT + connfd LT
    listenTrigMode = 0;  //listenfd触发模式，默认LT
    connTrigMode = 0;  //connfd触发模式，默认LT   
    opt_linger = 0;  //优雅关闭链接，默认不使用 
    sql_num = 8;  //数据库连接池数量,默认8
    thread_num = 8;   //线程池内的线程数量,默认8   
    close_log = 0;  //关闭日志,默认不关闭 
    actor_model = 0;  //并发模型,默认是proactor
}

void Config::parse_arg(int argc, char*argv[]){
    int opt;
    const char *str = "p:l:m:o:s:t:c:a:";
    while ((opt = getopt(argc, argv, str)) != -1)
    {
        switch (opt)
        {
        case 'p':
        {
            port = atoi(optarg);
            break;
        }
        case 'l':
        {
            logWrite = atoi(optarg);
            break;
        }
        case 'm':
        {
            trigMode = atoi(optarg);
            break;
        }
        case 'o':
        {
            opt_linger = atoi(optarg);
            break;
        }
        case 's':
        {
            sql_num = atoi(optarg);
            break;
        }
        case 't':
        {
            thread_num = atoi(optarg);
            break;
        }
        case 'c':
        {
            close_log = atoi(optarg);
            break;
        }
        case 'a':
        {
            actor_model = atoi(optarg);
            break;
        }
        default:
            break;
        }
    }
}