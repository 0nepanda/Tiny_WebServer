#ifndef LOG_H
#define LOG_H

#include <stdio.h>
#include <iostream>
#include <string>
#include <stdarg.h>
#include <pthread.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "block_queue.hpp" /* 阻塞队列 */
using namespace std;

//这四个宏定义在其他文件中使用，主要用于不同类型的日志输出
#define LOG_DEBUG(format, ...) Log::get_instance()->write_log(0, format, __VA_ARGS__)
#define LOG_INFO(format, ...) Log::get_instance()->write_log(1, format, __VA_ARGS__)
#define LOG_WARN(format, ...) Log::get_instance()->write_log(2, format, __VA_ARGS__)
#define LOG_ERROR(format, ...) Log::get_instance()->write_log(3, format, __VA_ARGS__)

class Log {
public:
    /* 获取单例模式的唯一实例接口 */
    static Log* get_instance() {
        static Log instance;
        return & instance;
    }

    static void *flush_log_thread(void* args) {
        Log::get_instance()->async_write_log();
        return nullptr;
    }

    bool init(const char* file_name, int close_log, int log_buf_size = 8192, int split_lines = 5000000, int max_queue_size = 0);

    void write_log(int level, const char* format, ...);

    void flush(void);

private:
    /* 私有化构造函数 */
    Log() {
        m_count = 0;
        m_is_async = false;
    }
    virtual ~Log() {
        if (m_fp != nullptr) {
            fclose(m_fp);
        }
    }
    /* 从阻塞队列中异步写入日志文件 */
    void *async_write_log() {
        string single_log;
        /* 从阻塞队列中取出一个日志 string，写入文件 */
        while (m_log_queque->pop(single_log)) {
            m_mutex.lock();
            fputs(single_log.c_str(), m_fp);
            m_mutex.unlock();
        }
        return nullptr;
    }

private:
    char dir_name[128];   /* 路径名 */
    char log_name[128];   /* log文件名 */
    int m_split_lines;    /* 日志最大行数 */
    int m_log_buf_size;   /* 日志缓冲区大小 */
    long long m_count;    /* 日志行数记录 */
    int m_today;          /* 将日志按天分类，记录当前是哪一天 */
    FILE* m_fp;           /* 打开 log 的文件指针 */
    char* m_buf;
    block_queue<string>* m_log_queque; /* 阻塞队列 */
    bool m_is_async;                   /* 是否同步标识位 */
    locker m_mutex;
    int m_close_log;  /* 关闭日志 */
};

#endif
