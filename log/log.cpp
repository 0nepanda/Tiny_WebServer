#include "log.h"

/* 异步需要设置阻塞队列的长度，同步不需要设置，异步才需要使用阻塞队列 */
bool Log::init(const char* file_name, int close_log, int log_buf_size, int split_lines, int max_queue_size) {
    /* 如果设置了 max_queue_size， 则设置为异步 */
    if (max_queue_size >= 1) {
        m_is_async = true;
        m_log_queque = new block_queue<string>(max_queue_size);
        pthread_t pid;
        /* 创建一个线程去写将阻塞队列中的 sting 写入日志 */
        pthread_create(&pid, nullptr, flush_log_thread, nullptr);
    }

    m_close_log = close_log;
    m_log_buf_size = log_buf_size;
    m_buf = new char[m_log_buf_size];
    memset(m_buf, '\0', m_log_buf_size);
    m_split_lines = split_lines;

    time_t t = time(nullptr);
    struct tm *sys_tm = localtime(&t); /* 转换为当前时间 */
    struct tm my_tm = *sys_tm;

    const char* p = strrchr(file_name, '/'); /* 指向 fila_name 中最后一次出现 / 的位置 */
    char log_full_name[512] = {0};

    if (p == nullptr) {
        /* p为空说明 filename 中没有 /， 将整个 file_name 写入 log_full_name */
        snprintf(log_full_name, 511, "%d_%02d_%02d_%s", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, file_name);
    }
    else {
        strcpy(log_name, p + 1); /* 截取 / 之后的部分为 log_name */
        strncpy(dir_name, file_name, p - file_name + 1); /* 截取 / 之前的部分为 dir_name */
        snprintf(log_full_name, 511, "%s%d_%02d_%02d_%s", dir_name, my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, log_name);
    }

    m_today = my_tm.tm_mday;

    m_fp = fopen(log_full_name, "a"); /* "a" 参数是向文件末尾追加写入 */
    if (m_fp == nullptr) {
        return false;
    }
    return true;
}

/* time和gettimeofday都可以获取日历时间，但gettimeofday提供更高的精度，可到微秒级 */
void Log::write_log(int level, const char* format, ...) {
    struct timeval now = {0, 0};
    gettimeofday(&now, nullptr);
    time_t t = now.tv_sec;
    struct tm *sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm; /* my_tm 为当前时间结构体 */
    char s[16] = {0};
    switch (level) {
        case 0: {
            strcpy(s, "[debug]:");
            break;
        }
        case 1: {
            strcpy(s, "[info]:");
            break;
        }
        case 2: {
            strcpy(s, "[warn]:");
            break;
        }
        case 3: {
            strcpy(s, "[error]:");
            break;
        }
        default: {
            strcpy(s, "[info]:");
            break;
        }
    }

    /* 写入一个 log 时，对 m_count 加 1， 防止超出 m_split_lines 最大行数 */
    m_mutex.lock();
    m_count++;

    /* 新的一天或者是超过最大行数，需要分文件了 */
    if (m_today != my_tm.tm_mday || m_count % m_split_lines == 0) {
        char new_log[512] = {0};
        fflush(m_fp); /* 避免文件流中还有残留的数据 */
        fclose(m_fp); /* 把原文件描述符（已写满或非今天）关闭 */
        char tail[16] = {0};

        snprintf(tail, 16, "%d_%02d_%02d_", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday);

        /* 如果 成员变量 m_today 不是今天， 说明这是今天第一次写入日志文件 */
        if (m_today != my_tm.tm_mday) {
            snprintf(new_log, 511, "%s%s%s", dir_name, tail, log_name);
            m_today = my_tm.tm_mday;
            m_count = 0;
        }
        /* 否则说明超过最大行了需要分文件 */
        else {
            snprintf(new_log, 511, "%s%s%s.%lld", dir_name, tail, log_name, m_count / m_split_lines);
        }
        m_fp = fopen(new_log, "a");
    }
    m_mutex.unlock();

    va_list valist;
    va_start(valist, format);

    string log_str;
    m_mutex.lock();

    /* 写入的具体时间内容格式 */
    int n = snprintf(m_buf, 48, "%d-%02d-%02d %02d:%02d:%02d.%06ld %s", 
                     my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_wday,
                     my_tm.tm_hour, my_tm.tm_min, my_tm.tm_sec, now.tv_usec, s);
    int m = vsnprintf(m_buf + n, m_log_buf_size - n - 1, format, valist);
    m_buf[m + n] = '\n';
    m_buf[m + n + 1] = '\0';
    log_str = m_buf;
    m_mutex.unlock();

    if (m_is_async && !m_log_queque->full()) {
        m_log_queque->push(log_str);
    }
    else {
        m_mutex.lock();
        fputs(log_str.c_str(), m_fp);
        m_mutex.unlock();
    }
    va_end(valist);
}

void Log::flush(void) {
    m_mutex.lock();
    fflush(m_fp);
    m_mutex.unlock();
}
