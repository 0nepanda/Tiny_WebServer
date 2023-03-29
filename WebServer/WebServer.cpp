#include "WebServer.h"

WebServer::WebServer() {
    /* http_conn 类对象*/
    users = new http_conn[MAX_FD];

    /* root 文件夹路径 */
    char server_path[200];
    getcwd(server_path, 200); /* 获取绝对路径 */
    char root[6] = "/root";
    m_root = (char*)malloc(strlen(server_path) + strlen(root) + 1);
    strcpy(m_root, server_path);
    strcat(m_root, root);

    /* 定时器 */
    users_timer = new client_data[MAX_FD];
}

WebServer::~WebServer() {
    close(m_epollfd);
    close(m_listenfd);
    close(m_pipefd[1]);
    close(m_pipefd[0]);
    delete[] users;
    delete m_pool;
}

void WebServer::init(int port, string user, string passWord, string dataBaseName, 
              int log_write , int opt_linger, int trigMode, int sql_num, 
              int thread_num, int close_log, int actor_model) {
    m_port = port;
    m_user = user;
    m_passWord = passWord;
    m_dataBaseName = dataBaseName;
    m_sql_num = sql_num;
    m_thread_num = thread_num;
    m_log_write = log_write;
    m_OPT_LINGER = opt_linger;
    m_TRIGMode = trigMode;
    m_close_log = close_log;
    m_actormodel = actor_model;
}

void WebServer::trig_mode() {
    /* LT + LT*/
    if (m_TRIGMode == 0) {
        m_LISTENTrigmode = 0;
        m_CONNTrigmode = 0;
    }
    /*LT + ET*/
    else if (m_TRIGMode == 1) {
        m_LISTENTrigmode = 0;
        m_CONNTrigmode = 1;
    }
    /* ET + LT*/
    else if (m_TRIGMode == 2) {
        m_LISTENTrigmode = 1;
        m_CONNTrigmode = 0;
    }
    /* ET + ET */
    else if (m_TRIGMode == 3) {
        m_LISTENTrigmode = 1;
        m_CONNTrigmode = 1;
    }
}

void WebServer::log_write() {
    if (m_close_log == 0) {
        /* 初始化日志 */
        if (m_log_write == 1) {
            /* 异步 */
            Log::get_instance()->init("./ServerLog", m_close_log, 2000, 800000, 800);
        }
        else {
            /* 同步 */
            Log::get_instance()->init("./ServerLog", m_close_log, 2000, 800000, 0);
        }
    }
}

void WebServer::sql_pool() {
    /* 初始化数据库连接池 */
    m_connPool = connection_pool::GetInstance();
    m_connPool->init("localhost", m_user, m_passWord, m_dataBaseName, 3306, m_sql_num, m_close_log);
    /* 初始化数据库读取表 */
    users->initmysql_result(m_connPool);
}

void WebServer::thread_pool() {
    /* 创建线程池 */
    m_pool = new threadpool<http_conn>(m_actormodel, m_connPool, m_thread_num);
}

void WebServer::eventListen() {
    /* 网络编程基本步骤 */
    m_listenfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(m_listenfd >= 0);

    /* 优雅关闭连接 */
    if (m_OPT_LINGER == 0) {
        struct linger tmp = {0, 1};
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }
    else if (m_OPT_LINGER == 1) {
        struct linger tmp = {1, 1};
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }

    int ret = 0;
    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(m_port);

    /* 忽略 TIME_OUT */
    int flag = 1;
    setsockopt(m_listenfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));
    /* 绑定 ip 地址，端口号，开启监听 */
    ret = bind(m_listenfd, (struct sockaddr*)&address, sizeof(address));
    assert(ret >= 0);
    ret = listen(m_listenfd, 5);
    assert(ret >= 0);

    utils.init(TIMESLOT);

    /* epoll 创建内核事件表 */
    epoll_event events[MAX_EVENT_NUMBER];
    m_epollfd = epoll_create(5);
    assert(ret != -1);

    utils.addfd(m_epollfd, m_listenfd, false, m_LISTENTrigmode);
    http_conn::m_epollfd = m_epollfd;

    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, m_pipefd);
    assert(ret != -1);
    utils.setnonblocking(m_pipefd[1]);
    utils.addfd(m_epollfd, m_pipefd[0], false, 0);

    utils.addsig(SIGPIPE, SIG_IGN);
    utils.addsig(SIGALRM, utils.sig_handler, false);
    utils.addsig(SIGTERM, utils.sig_handler, false);

    alarm(TIMESLOT);

    /* 工具类 */
    Utils::u_pipefd = m_pipefd;
    Utils::u_epollfd;
}

/* 给新连接的客户创建一个定时器，加入升序链表中 */
void WebServer::timer(int connfd, struct sockaddr_in client_address) {
    users[connfd].init(connfd, client_address, m_root, m_CONNTrigmode, m_close_log, m_user, m_passWord, m_dataBaseName);

    /* 初始化定时器相关数据 */
    users_timer[connfd].address = client_address;   /* 客户端地址 */
    users_timer[connfd].sockfd = connfd;            /* 客户端文件描述符 */
    util_timer* timer = new util_timer;             /* 创建一个定时器 */
    timer->user_data = &users_timer[connfd];        /* 定时器的连接资源为刚连接的客户端 */
    timer->cb_func = cb_func;                       /* 设置定时器的回调函数 */
    time_t cur = time(nullptr);                     /* 记录当前时间 */
    timer->expire = cur + 3 * TIMESLOT;             /* 将此定时器的超时时间设置为 当前时间 + 3 * TIMESLOT */
    users_timer[connfd].timer = timer;              /* 设置当前客户端的定时器为刚设置好的定时器 */
    utils.m_timer_lst.add_timer(timer);             /* 将此定时器添加到升序链表中 */
}

/* 若有数据传输时，则将定时器往后延 3 个单位， 并对新的定时器在链表上的位置进行调整 */
void WebServer::adjust_timer(util_timer* timer) {
    time_t cur = time(nullptr);
    timer->expire = cur + 3 * TIMESLOT;
    utils.m_timer_lst.adjust_timer(timer);

    LOG_INFO("%s", "adjust timer once");
}

/* 定时器到期，关闭连接 */
void WebServer::deal_timer(util_timer* timer, int sockfd) {
    timer->cb_func(&users_timer[sockfd]);
    if (timer) {
        utils.m_timer_lst.del_timer(timer);
    }
    LOG_INFO("close fd %d", users_timer[sockfd].sockfd);
}

/* 处理客户端连接 */
bool WebServer::dealclientdata() {
    struct sockaddr_in client_address;
    socklen_t client_addrlength = sizeof(client_address);

    if (m_LISTENTrigmode == 0) {
        int connfd = accept(m_listenfd, (struct sockaddr*)&client_address, &client_addrlength);
        if (connfd < 0) {
            LOG_ERROR("accept error: errno is: %d", errno);
            return false;
        }
        if (http_conn::m_user_count >= MAX_FD) {
            utils.show_error(connfd, "Internal server busy");
            LOG_ERROR("%s", "Internal server busy");
            return false;
        }
        timer(connfd, client_address); /* 连接成功，初始化该连接并且创建定时器 */
    }
    else {
        while (1) {
            int connfd = accept(m_listenfd, (struct sockaddr*)&client_address, &client_addrlength);
            if (connfd < 0) {
                LOG_ERROR("accept error: errno is: %d", errno);
                break;
            }
            if (http_conn::m_user_count >= MAX_FD) {
                utils.show_error(connfd, "Internal server busy");
                LOG_ERROR("%s", "Internal server busy");
                break;
            }
            timer(connfd, client_address); /* 连接成功，创建定时器 */
        }
        return false;
    }
    return true;
}

/* 处理信号 */
bool WebServer::dealwithsignal(bool& timeout, bool& stop_server) {
    int ret = 0;
    int sig;
    char signals[1024];
    ret = recv(m_pipefd[0], signals, sizeof(signals), 0);
    if (ret == -1) {
        return false;
    }
    else if (ret == 0) {
        return false;
    }
    else {
        for (int i = 0; i < ret; i++) {
            switch(signals[i]) {
                case SIGALRM: {
                    timeout = true;
                    break;
                }
                case SIGTERM: {
                    stop_server = true;
                    break;
                }
            }
        }
    }
    return true;
}

/* 两种事件处理模式 处理 读数据 */
void WebServer::dealwithread(int sockfd) {
    util_timer* timer = users_timer[sockfd].timer;

    /* reactor模式 */
    if (m_actormodel == 1) {
        /* 将定时器延迟 */
        if (timer) {
            adjust_timer(timer);
        }
        /* 检测到读事件，将该事件放入请求队列 */
        m_pool->append(users + sockfd, 0);

        while (true) {
            if (users[sockfd].improv == 1) {
                if (users[sockfd].timer_flag == 1) {
                    deal_timer(timer, sockfd);
                    users[sockfd].timer_flag = 0;
                }
                users[sockfd].improv = 0;
                break;
            }
        }
    }
    /* proactor 模式 */
    else {
        if (users[sockfd].read()) {
            LOG_INFO("deal with the clint(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));
            /* 读完之后将任务交给工作线程 */
            m_pool->append_p(users + sockfd);
            if (timer) {
                adjust_timer(timer);
            }
        }
        else {
            deal_timer(timer, sockfd);
        }
    }
}

/* 两种事件处理模式处理 写 */
void WebServer::dealwithwrite(int sockfd) {
    util_timer* timer = users_timer[sockfd].timer;

    /* reactor 模式*/
    if (m_actormodel == 1) {
        if (timer) {
            adjust_timer(timer);
        }
        m_pool->append(users + sockfd, 1);

        while(true) {
            if (users[sockfd].improv == 1) {
                if (users[sockfd].timer_flag) {
                    deal_timer(timer, sockfd);
                    users[sockfd].timer_flag = 0;
                }
                users[sockfd].improv = 0;
                break;
            }
        }
    }
    /* proactor 模式 */
    else {
        if (users[sockfd].write()) {
            LOG_INFO("send data to the client(%s)",inet_ntoa(users[sockfd].get_address()->sin_addr));

            if (timer) {
                adjust_timer(timer);
            }
        }
        else {
            deal_timer(timer, sockfd);
        }
    }
}

void WebServer::eventLoop() {
    bool timeout = false;
    bool stop_server = false;

    while (!stop_server) {
        int number = epoll_wait(m_epollfd, events, MAX_EVENT_NUMBER, -1);
        if (number < 0 && errno != EINTR) {
            LOG_ERROR("%s", "epoll failure");
            break;
        }

        for (int i = 0; i < number; i++) {
            int sockfd = events[i].data.fd;

            /* 处理新到的客户端连接 */
            if (sockfd == m_listenfd) {
                bool flag = dealclientdata();
                if (flag == false) {
                    continue;
                }
            }
            /* 事件出错 */
            else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                /* 服务端关闭连接，移除对应的定时器 */
                deal_timer(users_timer[sockfd].timer, sockfd);
            }
            /* 处理信号 */
            else if((sockfd == m_pipefd[0]) && (events[i].events & EPOLLIN)) {
                bool flag = dealwithsignal(timeout, stop_server);
                if (flag = false) {
                    LOG_ERROR("%s", "dealclientdata failure");
                }
            }
            /* 处理读事件 */
            else if(events[i].events & EPOLLIN) {
                dealwithread(sockfd);
            }
            /* 处理写事件 */
            else if(events[i].events & EPOLLOUT) {
                dealwithwrite(sockfd);
            }
        }
        if (timeout) {
            utils.timer_handler();
            LOG_INFO("%s", "timer tick");
            timeout = false;
        }
    }
}