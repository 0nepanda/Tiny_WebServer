#include "./config/config.h"

int main(int argc, char* argv[]) {
    /* 需要修改的数据库信息，登录名，密码，库名 */
    string user = "root";
    string passwd = "yetao1996";
    string dataBaseName = "webserver";

    /* 命令行解析 */
    Config config;
    config.parse_arg(argc, argv);

    WebServer server;

    /* 初始化 */
    server.init(config.port, user, passwd, dataBaseName, config.logWrite,
                config.opt_linger, config.trigMode, config.sql_num, config.thread_num,
                config.close_log, config.actor_model);

    server.log_write(); /* 日志 */
    server.sql_pool();  /* 数据库 */
    server.thread_pool(); /* 线程池 */
    server.trig_mode();  /* 触发模式 */
    server.eventListen(); /* 监听 */
    server.eventLoop();   /* 运行 */

    return 0;
}