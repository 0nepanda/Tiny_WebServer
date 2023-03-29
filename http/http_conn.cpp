#include "http_conn.h"
#include <mysql/mysql.h>
#include <fstream>

/* 定义 HTTP 响应 的一些状态信息 */
const char* ok_200_title = "OK";
const char* error_400_title = "Bad Request";
const char* error_400_form = "Your request has bad syntax or is inherently impossible to staisfy.\n";
const char* error_403_title = "Forbidden";
const char* error_403_form = "You do not have permission to get file form this server.\n";
const char* error_404_title = "Not Found";
const char* error_404_form = "The requested file was not found on this server.\n";
const char* error_500_title = "Internal Error";
const char* error_500_form = "There was an unusual problem serving the request file.\n";

locker m_lock;
map<string, string> users;

/* 从 mysql 数据库的 user 表中取出用户名、密码，存入 map 中 */
void http_conn::initmysql_result(connection_pool* connPool) {
    /* 先从连接池中取出一个连接 */
    MYSQL* mysql = nullptr;
    connectionRAII mysqlcon(&mysql, connPool);

    /* 在 user 表中检索 username，passwd数据， 浏览器端输入 */
    if (mysql_query(mysql, "SELECT username, passwd from user")) {
        LOG_ERROR("SELECT error:%s\n", mysql_error(mysql));
    }

    /* 从表中检索完整的结果集 */
    MYSQL_RES* result = mysql_store_result(mysql);

    /* 返回结果集中的列数 */
    int num_fields = mysql_num_fields(result);

    /* 返回所有字段结果组成的数组 */
    MYSQL_FIELD *fields = mysql_fetch_fields(result);

    /* 从结果集中获取下一行，将对应的用户名、密码存入 map 中 */
    while (MYSQL_ROW row = mysql_fetch_row(result)) {
        string temp1(row[0]);
        string temp2(row[1]);
        users[temp1] = temp2;
    }
}

/* 使用 fcntl 函数设置文件描述符为非阻塞 IO */
/* 传入参数为：文件描述符fd */
int setnonblocking(int fd) {
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

/* 向 epollfd 中注册监视对象文件描述符 fd，以及设置监视事件*/
/* 传入参数为：epoll文件描述符 epollfd，需要注册的监视对象文件描述符 fd，是否 one_shot */
/* 根据 TRIGMode参数 */
void addfd(int epollfd, int fd, bool one_shot, int TRIGMode) {
    epoll_event event;
    event.data.fd = fd;

    if (TRIGMode == 1) {
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;  /* 需要读取数据的情况 | 以边缘触发的方式得到事件通知 | 断开连接或半关闭的情况 */
    }
    else {
        event.events = EPOLLIN | EPOLLRDHUP;
    }
    if (one_shot) {
        event.events |= EPOLLONESHOT; /* 发生一次事件后，相应的文件描述符不再收到事件通知 */
    }
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

/* 从 epollfd 中删除监视的文件描述符 fd, 并关闭文件描述符 fd */
/* 传入参数为：epoll文件描述符 epollfd，需要删除的监视对象文件描述符 fd*/
void removefd(int epollfd, int fd) {
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close (fd);
}

/* 重置 EPOLLONESHOT 事件 */
/* 传入参数为：epoll文件描述符 epollfd，需要重置的监视对象文件描述符 fd，该 fd 监视的事件类型 */
void modfd(int epollfd, int fd, int ev, int TRIGMode) {
    epoll_event event;
    event.data.fd = fd;
    if (TRIGMode == 1) {
        event.events = ev | EPOLLONESHOT | EPOLLET | EPOLLRDHUP;  /* 需要读取数据的情况 | 以边缘触发的方式得到事件通知 | 断开连接或半关闭的情况 */
    }
    else {
        event.events = ev | EPOLLONESHOT | EPOLLRDHUP;
    }
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

/* 类外初始化静态变量：epollfd文件描述符初始化为 -1，用户数量初始化为 0 */
int http_conn::m_user_count = 0;
int http_conn::m_epollfd = -1;

/* 成员函数的实现：关闭 HTTP 连接*/
void http_conn::close_conn(bool real_close) {
    if (real_close && (m_epollfd != -1)) {
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        m_user_count--;  /* 关闭一个连接时，将客户总量 -1 */
    }
}

/* 初始化客户 HTTP 连接， 并将客户文件描述符加入 epollfd中监视 */
/* 传入参数为：客户的 文件描述符 socket，客户的地址 addr */
void http_conn::init(int sockfd, const sockaddr_in& addr, char* root, int TRIGMode,
                     int close_log, string user, string passwd, string sqlname) {
    m_sockfd = sockfd;
    m_address = addr;
    /* 以下两行是为了避免 TIME_WAIT 状态，仅用于调试，实际使用时应去掉 */
    int reuse = 1;
    setsockopt(m_sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    addfd(m_epollfd, sockfd, true, m_TRIGMode);
    m_user_count++;

    /* 当浏览器出现连接重置时，可能时网站根目录出错或 http 响应格式出错或文件中的内容完全为空 */
    doc_root = root;
    m_TRIGMode = TRIGMode;
    m_close_log = close_log;

    strcpy(sql_user, user.c_str());
    strcpy(sql_passwd, passwd.c_str());
    strcpy(sql_name, sqlname.c_str());

    init();
}
/* 初始化一些参数信息 */
void http_conn::init() {
    mysql = nullptr;

    bytes_to_send = 0;
    bytes_have_send = 0;

    m_check_state = CHECK_STATE_REQUESTLINE;
    m_linger = false;

    m_method = GET;
    m_url = 0;
    m_version = 0;
    m_content_length = 0;
    m_host = 0;
    m_start_line = 0;
    m_checked_idx = 0;
    m_read_idx = 0;
    m_write_idx = 0;
    cgi = 0;
    m_state = 0;
    timer_flag = 0;
    improv = 0;
    memset(m_read_buf, '\0', READ_BUFFER_SIZE);
    memset(m_write_buf, '\0', WRITE_BUFFER_SIZE);
    memset(m_real_file, '\0', FILENAME_LEN);
}

/* 从状态机：用于分析出一行数据，并不是取出数据 */
http_conn::LINE_STATUS http_conn::parse_line() {
    char temp;
    for (; m_checked_idx < m_read_idx; m_checked_idx++) {
        temp = m_read_buf[m_checked_idx];
        if (temp == '\r') {
            /* \r后面至少还有个\n，所以接下来达到了 buffer 末尾表示 buffer 还需要继续接收，返回 LINE_OPEN */
            if ((m_checked_idx + 1) == m_read_idx) {
                return LINE_OPEN;
            }
            /* 接下来的字符是\n，将\r\n修改成\0\0，将m_checked_idx指向下一行的开头，返回 LINE_OK */
            else if (m_read_buf[m_checked_idx + 1] == '\n') {
                m_read_buf[m_checked_idx++] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
        /* 一般是上次读取到\r就到了buffer末尾，没有接收完整，再次接收时会出现这种情况 */
        else if (temp == '\n') {
            if ((m_checked_idx > 1) && (m_read_buf[m_checked_idx - 1] == '\r')) {
                m_read_buf[m_checked_idx - 1] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    /* 当前字符既不是 \r,也不是 \n，说明接收不完整，需要继续接收，返回 LINE_OPEN */
    return LINE_OPEN;
}

/* 循环读取客户数据，直到无数据可读或者对方关闭连接 */
/* 非阻塞 ET 工作模式下，需要一次性将数据读完 */
bool http_conn::read() {
    if(m_read_idx >= READ_BUFFER_SIZE) {
        return false;
    }
    int bytes_read = 0;

    /* LT 模式读取 */
    if (m_TRIGMode == 0) {
        bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
        m_read_idx += bytes_read;
        if (bytes_read <= 0) {
            return false;
        }
        return true;
    }
    /* ET 模式读取 */
    else {
        while (true) {
            /* 从套接字接收数据，存储在 m_read_buf 缓冲区 */
            bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
            if (bytes_read == -1) {
                /* 非阻塞 ET 模式下，需要一次性将数据读完 */
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    break;
                }
                return false;
            }
            else if(bytes_read == 0) {
                return false;
            }
            /* 修改 m_read_idx 的读取字节数 */
            m_read_idx += bytes_read;
        }
        return true;
    }
}

/* 解析 HTTP 请求行，获得请求方法、目标 URL，以及 HTTP 版本号 */
http_conn::HTTP_CODE http_conn::parse_request_line(char* text) {
    /* strpbrk是在源字符串（s1）中找出最先含有搜索字符串（s2）中任一字符的位置并返回，若找不到则返回空指针。*/
    m_url = strpbrk(text, " \t"); /* 请求行中最先含有空格和 \t 任一字符的位置并返回 */
    if(! m_url) {
        return BAD_REQUEST;
    }
    *m_url++ = '\0'; /* 将该位置改为 \0， 用于将前面的数据取出 */
    /* 取出数据，并与 GET 和 POST 相比，已确定请求方式*/
    char* method = text;
    if (strcasecmp(method, "GET") == 0) {
        m_method = GET;
    }
    else if (strcasecmp(method, "POST") == 0) {
        m_method = POST;
        cgi = 1;
    }
    else {
        return BAD_REQUEST;
    }

    /* strspn 检索字符串 str1 中第一个不在字符串 str2 中出现的字符下标。*/
    m_url += strspn(m_url, " \t"); /* 不知道 GET 和 url 之间还有没有空格了，如果有跳过这些空格*/
    m_version = strpbrk(m_url, " \t"); /* 与上面得到 GET 同理 */
    if (! m_version) {
        return BAD_REQUEST;
    }
    *m_version++ = '\0';
    m_version += strspn(m_version, " \t"); /* 此时正常情况下：m_url 指向 url，m_version 指向 HTTP/1.1 */
    /* 比较 m_version 是否为 HTTP/1.1 */
    if (strcasecmp(m_version, "HTTP/1.1") != 0) {
        return BAD_REQUEST;
    }
    /* 当前 m_url 已经指向目标 URL，但是对目标 URL 进行处理*/
    if (strncasecmp (m_url, "http://", 7) == 0) {
        m_url += 7;
        /* 在 m_url 中寻找字符 '/' 第一次出现的位置，并返回其位置，若失败则返回NULL */
        m_url = strchr(m_url, '/');
    }
    /* 同样增加 https 的情况 */
    if (strncasecmp(m_url, "https://", 8) == 0) {
        m_url += 8;
        m_url = strchr(m_url, '/');
    }
    if (!m_url || m_url[0] != '/') {
        return BAD_REQUEST;
    }
    /* 当 url 为 / 时，显示判断页面 */
    if (strlen(m_url) == 1) {
        strcat(m_url, "judge.html");
    }
    m_check_state = CHECK_STATE_HEADER; /* 处理完请求行，将主状态机状态转移处理请求头 */
    return NO_REQUEST; /* 需要继续读取请求头等信息 */
}

/* 解析 HTTP 请求的 “一个” 头部信息 */
http_conn::HTTP_CODE http_conn::parse_headers(char* text) {
    /* 遇到空行，表示头部字段解析完毕 */
    if (text[0] == '\0') { /* 从状态机读取一行数据时，已将 \r\n 都修改为 \0 */
        /* 如果 HTTP 请求有消息体，则还需要读取 m_content_length 字节的消息体，主状态机转移到 CHECK_STATE_CONTENT */
        if (m_content_length != 0) {
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        /* 否则说明我们已经得到一个完整的 HTTP 请求 */
        return GET_REQUEST;
    }

    /* 解析头部连接字段 Connection */
    else if(strncasecmp(text, "Connection:", 11) == 0) {
        text += 11;
        text += strspn(text, " \t");
        if (strcasecmp(text, "Keep-Alive") == 0) {
            m_linger = true;
        }
    }

    /* 解析 请求体内容长度 字段：Conten-Length */
    else if (strncasecmp(text, "Content-Length:", 15) == 0) {
        text += 15;
        text += strspn(text, " \t");
        m_content_length = atol(text);
    }

    /* 解析请求头部 Host 字段 */
    else if (strncasecmp(text, "Host:", 5) == 0) {
        text += 5;
        text += strspn(text, " \t");
        m_host = text;
    }
    else {
        LOG_INFO("oop! unknow header %s", text);
    }
    return NO_REQUEST;
}

/* 判断 http 请求是否被完整的读入了，将请求体内容存入 m_string */
http_conn::HTTP_CODE http_conn::parse_content(char* text) {
    if (m_read_idx >= (m_content_length + m_checked_idx)) {
        text[m_content_length] = '\0';
        /* POST请求中最后为输入的用户名和密码 */
        m_string = text;
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

/* 主状态机 */
http_conn::HTTP_CODE http_conn::process_read() {
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char* text = 0;
    /* 从 read_buf 中取出一行一行数据 */
    while (((m_check_state == CHECK_STATE_CONTENT) && (line_status == LINE_OK)) || ((line_status = parse_line()) == LINE_OK)) {
        text = get_line();
        m_start_line = m_checked_idx;
        LOG_INFO("got a http line: %s", text);
        /*主状态机根据 m_check_state 当前的状态来决定如何处理此行 text */
        switch (m_check_state) {
            case CHECK_STATE_REQUESTLINE: {
                ret = parse_request_line(text);
                if (ret == BAD_REQUEST) {
                    return BAD_REQUEST;
                }
                break;
            }
            case CHECK_STATE_HEADER: {
                ret = parse_headers(text);
                if (ret == BAD_REQUEST) {
                    return BAD_REQUEST;
                }
                else if (ret == GET_REQUEST) {
                    return do_request();
                }
                break;
            }
            case CHECK_STATE_CONTENT: {
                ret = parse_content(text);
                if (ret == GET_REQUEST) {
                    return do_request();
                }
                line_status = LINE_OPEN;
                break;
            }
            default: {
                return INTERNAL_ERROR;
            }
        }
    }
    return NO_REQUEST;
}

/* 当得到一个完整、正确的 HTTP 请求时，我们就分析目标文件的属性 */
/* 如果目标文件存在、且对所以用户可读，且不是目录，就使用 mmap 将其映射到内存地址 
   m_file_address 处，并告诉调用者获取文件成功 */
http_conn::HTTP_CODE http_conn::do_request() {
    strcpy(m_real_file, doc_root); /* 将 m_real_file 赋值为网站根目录 */
    int len = strlen(doc_root);
    const char* p = strrchr(m_url, '/');
    /* 同步校验 (处理cgi) */
    if (cgi == 1 && (*(p + 1) == '2' || *(p + 1) == '3')) {
        /* 根据标志判断是登录检测还是注册检测 */
        char* m_url_real = (char*)malloc(sizeof(char) * 200);

       /* 将用户名和密码提取出来 */ 
       char name[100], password[100];
       int i;
       for (i = 5; m_string[i] != '&'; i++) {
           name[i - 5] = m_string[i];
       }
       name[i - 5] = '\0';

       int j = 0;
       for (i = i + 10; m_string[i] != '\0'; i++, j++) {
           password[j] = m_string[i];
       }
       password[j] = '\0';

       if (*(p + 1) == '3') {
           /* 注册校验，先检测数据库中是否有重名的 */
           char *sql_insert = (char *)malloc(sizeof(char) * 200);
           strcpy(sql_insert, "INSERT INTO user(username, passwd) VALUES(");
           strcat(sql_insert, "'");
           strcat(sql_insert, name);
           strcat(sql_insert, "', '");
           strcat(sql_insert, password);
           strcat(sql_insert, "');");
           /* 如果没有重名的，进行增加数据 */
           if (users.find(name) == users.end()) {
               m_lock.lock();
               int res = mysql_query(mysql, sql_insert);
               users.insert(pair<string, string>(name, password));
               m_lock.unlock();
               strcpy(m_url_real, "/log.html");
               strncpy(m_real_file + len, m_url_real, strlen(m_url_real));
           }
           else {
               strcpy(m_url_real, "/registerError.html");
               strncpy(m_real_file + len, m_url_real, strlen(m_url_real));
           }
       }
       else if (*(p + 1) == '2') {
           /* 如果是登录 ，直接判断 */
           if ((users.find(name) != users.end()) && (users[name] == password)) {
               strcpy(m_url_real, "/welcome.html");
               strncpy(m_real_file + len, m_url_real, strlen(m_url_real));
           }
           else {
               strcpy(m_url_real, "/logError.html");
               strncpy(m_real_file + len, m_url_real, strlen(m_url_real));
           }
       }
    }

    else if(*(p + 1) == '0') {
        char* m_url_real = (char*)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/register.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));
        free(m_url_real);
    }

    else if(*(p + 1) == '1') {
        char* m_url_real = (char*)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/log.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));
        free(m_url_real);
    }

    else if(*(p + 1) == '5') {
        char* m_url_real = (char*)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/picture.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));
        free(m_url_real);
    }

    else if(*(p + 1) == '6') {
        char* m_url_real = (char*)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/video.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));
        free(m_url_real);
    }

    else if(*(p + 1) == '7') {
        char* m_url_real = (char*)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/fans.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));
        free(m_url_real);
    }

    else if(*(p + 1) == '8') {
        char* m_url_real = (char*)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/judge.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));
        free(m_url_real);
    }

    else {
        /* 将 url 添加到 m_real_file 后，此时 m_real_file 为客户请求文件的绝对路径*/
        strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);
    }
    /* stat 函数用来获取文件信息 */
    /* 第一个参数为 文件路径， 第二个参数为 stat 类型的结构体，执行成功保存文件的相关属性 */
    /* 执行成功返回 0，执行失败返回 -1 */
    if (stat(m_real_file, &m_file_stat) < 0) {
        return NO_RESOURCE;
    }
    /* S_IROTH 00004 其他用户具可读取权限 */
    if (!(m_file_stat.st_mode & S_IROTH)) {
        return FORBIDDEN_REQUEST; /* 说明该文件其他用户不可读 */
    }
    /* 判断是否为目录文件 */
    if (S_ISDIR(m_file_stat.st_mode)) {
        return BAD_REQUEST;
    }
    int fd = open(m_real_file, O_RDONLY);
    /* mmap函数详解：https://blog.csdn.net/bhniunan/article/details/104105153 */
    m_file_address = (char*)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    return FILE_REQUEST; 
}
/* 对内存映射区执行 unmap 操作 */
void http_conn::unmap() {
    if (m_file_address) {
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = 0;
    }
}

/* 写 HTTP 响应 */
bool http_conn::write() {
    int temp = 0;
    
    if (bytes_to_send == 0) {
        modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);
        init();
        return true;
    }
    
    while (1) {
        temp = writev(m_sockfd, m_iv, m_iv_count);
        if (temp < 0) {
            /* 如果 TCP 写缓冲没有空间，则等待下一轮 EPOLLOUT 事件 */
            if (errno == EAGAIN) {
                modfd(m_epollfd, m_sockfd, EPOLLOUT, m_TRIGMode);
                return true;
            }
            unmap();
            return false;
        }
        /* 写成功更新待写和已写的字节量 */
        bytes_to_send -= temp;
        bytes_have_send += temp;
        
        if (bytes_have_send >= m_iv[0].iov_len)
        {
            m_iv[0].iov_len = 0;
            m_iv[1].iov_base = m_file_address + (bytes_have_send - m_write_idx);
            m_iv[1].iov_len = bytes_to_send;
        }
        else
        {
            m_iv[0].iov_base = m_write_buf + bytes_have_send;
            m_iv[0].iov_len = m_iv[0].iov_len - bytes_have_send;
        }

        if (bytes_to_send <= 0)
        {
            unmap();
            modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);

            if (m_linger)
            {
                init();
                return true;
            }
            else
            {
                return false;
            }
        }
        
    }
}

/* 往待写缓冲区中写入待发送的数据 */
bool http_conn::add_response(const char* format, ...) {
    if (m_write_idx >= WRITE_BUFFER_SIZE) {
        return false;
    }
    va_list arg_list; /* 定义可变参数列表，解决变参问题 */
    va_start(arg_list, format); /* 将变参列表初始化为传入参数 */
    /* vsnprintf : 将 可变参数 格式化输出 到一个字符数组 */
    /* 将数据 format 从可变参数列表写入缓冲区写，返回写入数据的长度 */
    int len = vsnprintf(m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - 1 - m_write_idx, format, arg_list);
    /* 如果写入的数据长度超过缓冲区剩余空间，则报错 */
    if (len >= (WRITE_BUFFER_SIZE - 1 - m_write_idx)) {
        va_end(arg_list);
        return false;
    }
    /* 更新写缓冲区待发送的字节数 */
    m_write_idx += len;
    /* 清空可变参数列表 */
    va_end(arg_list);

    LOG_INFO("request:%s", m_write_buf);

    return true;
}

/* 添加状态行 */
bool http_conn::add_status_line(int status, const char* title) {
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}

/* 添加消息报头：文本长度、连接状态、空行*/
bool http_conn::add_headers(int content_len) {
    bool ret = true;
    ret = ret && add_content_length(content_len);  /* 表示响应报文长度 */
    ret = ret && add_linger();  /* 连接状态：通知浏览器是保持连接还是关闭 */
    ret = ret && add_blank_line();
    return ret;
}
bool http_conn::add_content_length(int content_len) {
    return add_response("Content-Length: %d\r\n", content_len);
}
bool http_conn::add_linger() {
    return add_response("Connection: %s\r\n", (m_linger == true) ? "keep-alive" : "close");
}
bool http_conn::add_blank_line() {
    return add_response("%s", "\r\n");
}
/* 添加文本 content */
bool http_conn::add_content(const char* content) {
    return add_response("%s", content);
}

/* 根据服务器处理 HTTP 请求的结果，决定返回给客户端的内容 */
bool http_conn::process_write(HTTP_CODE ret) {
    switch (ret) {
        case INTERNAL_ERROR: {
            add_status_line(500, error_500_title);
            add_headers(strlen(error_500_form));
            if (!add_content(error_500_form)) {
                return false;
            }
            break;
        }
        case BAD_REQUEST: {
            add_status_line(400, error_400_title);
            add_headers(strlen(error_400_form));
            if (!add_content(error_400_form)) {
                return false;
            }
            break;
        }
        case NO_RESOURCE: {
            add_status_line(404, error_404_title);
            add_headers(strlen(error_404_form));
            if (!add_content(error_404_form)) {
                return false;
            }
            break;
        }
        case FORBIDDEN_REQUEST: {
            add_status_line(403, error_403_title);
            add_headers(strlen(error_403_form));
            if (!add_content(error_403_form)) {
                return false;
            }
            break;
        }
        case FILE_REQUEST: {
            add_status_line(200, ok_200_title);
            if (m_file_stat.st_size != 0) {
                add_headers(m_file_stat.st_size);
                m_iv[0].iov_base = m_write_buf;
                m_iv[0].iov_len = m_write_idx;
                m_iv[1].iov_base = m_file_address;
                m_iv[1].iov_len = m_file_stat.st_size;
                m_iv_count = 2;
                bytes_to_send = m_write_idx + m_file_stat.st_size;
                return true;
            }
            else {
                const char* ok_string = "<html><body></body></html>";
                add_headers(strlen(ok_string));
                if (!add_content(ok_string)) {
                    return false;
                }
            }
        }
        default: {
            return false;
        }
    }
    //除FILE_REQUEST状态外，其余状态只申请一个iovec，指向响应报文缓冲区
    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    bytes_to_send = m_write_idx;
    return true;
}

/* 由线程池中的工作线程调用，这是处理 HTTP 请求的入口函数 */
void http_conn::process() {
    HTTP_CODE read_ret = process_read(); /* 解析 HTTP 请求, 并返回解析结果 */
    if (read_ret == NO_REQUEST) {
        /* 如果是请求不完整，需要继续读取请求报文，将 epoll 事件重置等待剩余的请求报文 */
        modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);
        return;
    }

    bool write_ret = process_write(read_ret);
    if (!write_ret) {
        close_conn();
    }
    modfd(m_epollfd, m_sockfd, EPOLLOUT, m_TRIGMode);
}
