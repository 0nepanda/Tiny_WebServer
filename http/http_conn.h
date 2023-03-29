#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H

#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <map>

#include "../lock/locker.h"
#include "../log/log.h"
#include "../sql_conn_pool/sql_connection_pool.h"
#include "../timer/lst_timer.h"

class http_conn {
public:
    static const int FILENAME_LEN = 200; /* 文件名的最大长度 */
    static const int READ_BUFFER_SIZE = 2048;  /* 读缓冲区的大小 */
    static const int WRITE_BUFFER_SIZE = 1024; /* 写缓冲区的大小 */

    /* HTTP 请求方法， 但我们仅支持 GET */
    enum METHOD {
        GET = 0,
        POST,
        HEAD,
        PUT,
        DELETE,
        TRACE,
        OPTIONS,
        CONNECT,
        PATCH
    };
    /* 解析客户请求时， 主状态机三种状态：标识解析位置 */
    enum CHECK_STATE {
        CHECK_STATE_REQUESTLINE = 0,  /* 解析请求行 */
        CHECK_STATE_HEADER,    /* 解析请求头 */
        CHECK_STATE_CONTENT    /* 解析消息体，仅用于解析 POST 请求 */
    };
    /* 服务器处理 HTTP 请求的可能结果 */
    enum HTTP_CODE {
        NO_REQUEST,  /* 请求不完整，需要继续读取请求报文数据 */
        GET_REQUEST,  /* 获得了完整的HTTP请求 */
        BAD_REQUEST, /* HTTP请求报文有语法错误 */
        NO_RESOURCE,
        FORBIDDEN_REQUEST,
        FILE_REQUEST,
        INTERNAL_ERROR,  /* 服务器内部错误，该结果在主状态机逻辑 switch 的 default 下，一般不会触发 */
        CLOSED_CONNECTION
    };
    /* 从状态机三种状态：标识解析一行的读取状态 */
    enum LINE_STATUS {
        LINE_OK = 0,  /* 完整读取一行 */
        LINE_BAD,     /* 报文语法有误 */
        LINE_OPEN     /* 读取的行不完整 */
    };

public:
    http_conn() {}
    ~http_conn() {}

public:
    /* 初始化新接受的连接 */
    void init(int sockfd, const sockaddr_in& addr, char* root, int TRIGMode, int close_log, string user, string passwd, string sqlname);
    void close_conn(bool real_close = true); /* 关闭连接 */
    void process(); /* 处理客户请求 */
    bool read();   /* 非阻塞读操作 */
    bool write();  /* 非阻塞写操作 */
    sockaddr_in* get_address() {
        return &m_address;
    }
    void initmysql_result(connection_pool* connPool);
    int timer_flag;
    int improv;

private:
    void init(); /* 初始化连接 */
    HTTP_CODE process_read(); /* 解析 HTTP 请求 */
    bool process_write(HTTP_CODE ret);  /* 填充 HTTP 应答 */

    /* 下面这一组函数被 process_read 调用以分析 HTTP 请求 */
    HTTP_CODE parse_request_line(char* text);
    HTTP_CODE parse_headers(char* text);
    HTTP_CODE parse_content(char* text);
    HTTP_CODE do_request();
    char* get_line() {
        return m_read_buf + m_start_line;
    }
    LINE_STATUS parse_line();

    /* 下面这一组函数被 process_write 调用以填写 HTTP 应答 */
    void unmap();
    bool add_response(const char* format, ...);
    bool add_content(const char* content);
    bool add_status_line(int status, const char* title);
    bool add_headers(int content_length);
    bool add_content_length(int content_length);
    bool add_linger();
    bool add_blank_line();

public:
    /* 所有 socket 上的事件都被注册到同一个 epoll 内核事件表中，所以将 epoll 文件描述符设置为静态的 */
    static int m_epollfd;
    static int m_user_count;  /* 统计用户数量 */
    MYSQL* mysql;
    int m_state;  /* 0为读，1为写 */

private:
    int m_sockfd; /* 该 HTTP 连接的 socket */
    sockaddr_in m_address;  /* 该 HTTP 连接的对方的 socket 地址 */

    char m_read_buf[READ_BUFFER_SIZE];  /* 读缓冲区 */
    int m_read_idx; /* 标识读缓冲中已经读入的客户数据的最后一个字节的下一个位置 */
    int m_checked_idx;  /* 当前正在分析的字符在读缓冲区中的位置 */
    int m_start_line;  /* 当前正在解析的行的起始位置 */
    char m_write_buf[WRITE_BUFFER_SIZE]; /* 写缓冲区 */
    int m_write_idx;  /* 写缓冲区中待发送的字节数 */

    CHECK_STATE m_check_state;  /* 主状态机当前所处的状态 */
    METHOD m_method;    /* 请求方法 */

    char m_real_file[FILENAME_LEN];  /* 客户请求的目标文件的完整路径， 其内容等于 doc_root + m_url, doc_root 是网站根目录*/
    char* m_url; /* 客户请求的目标文件名 */
    char* m_version;  /* HTTP 协议版本号，我们仅支持 HTTP/1.1 */
    char* m_host; /* 主机名 */
    int m_content_length; /* HTTP 请求的消息体的长度 */
    bool m_linger; /* HTTP 请求是否要保持连接  */

    char* m_file_address;  /* 客户请求的目标文件被 mmap 到内存中的起始位置 */
    struct stat m_file_stat; /* 目标文件的状态，通过它我们可以判断文件是否存在、是否为目录、是否可读，并获取文件大小等信息 */

    /* 我们将采用 writev 来执行写操作， 所以定义下面两个成员，其中 m_iv_count 表示被写内存块的数量 */
    /* writev 函数可以将分散保存在多个缓冲中的数据一并发送 */
    struct iovec m_iv[2];
    int m_iv_count;

    int cgi;  /* 是否启用 POST */
    char* m_string;   /* 存储请求体数据 */

    int bytes_to_send;
    int bytes_have_send;

    char* doc_root;

    map<string, string> m_users;
    int m_TRIGMode;
    int m_close_log;

    char sql_user[100];
    char sql_passwd[100];
    char sql_name[100];
};
#endif