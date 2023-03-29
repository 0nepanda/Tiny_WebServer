#include "lst_timer.h"
#include "../http/http_conn.h"

sort_timer_lst::sort_timer_lst() {
    head = nullptr;
    tail = nullptr;
}

/* 销毁定时器容器（链表） */
sort_timer_lst::~sort_timer_lst() {
    util_timer* tmp = head;
    while(tmp) {
        head = head->next;
        delete tmp;
        tmp = head;
    }
}

/* 添加定时器到链表中 */
void sort_timer_lst::add_timer(util_timer* timer) {
    if (!timer) {
        return;
    }
    /* 若此时链表中还没有定时器 */
    if (!head) {
        head = tail = timer;
        return;
    }
    /* 若添加的定时器超时时间比头节点超时时间还小，插入头部 */
    if (timer->expire < head->expire) {
        timer->next = head;
        head->prev = timer;
        head = timer;
        return;
    }
    /* 否则调用重载的版本 */
    add_timer(timer, head);
}

void sort_timer_lst::adjust_timer(util_timer* timer) {
    if (!timer) {
        return;
    }
    util_timer* tmp = timer->next;
    if (!tmp || (timer->expire < tmp->expire)) {
        return;
    }
    if (timer == head) {
        head = head->next;
        head->prev = nullptr;
        timer->next = nullptr;
        add_timer(timer, head);
    }
    else {
        timer->prev->next = timer->next;
        timer->next->prev = timer->prev;
        add_timer(timer, timer->next);
    }
}

void sort_timer_lst::del_timer(util_timer* timer) {
    if (!timer) {
        return;
    }
    else if ((timer == head) && (timer == tail)) {
        delete timer;
        head = nullptr;
        tail = nullptr;
        return;
    }
    else if (timer == head) {
        head = head->next;
        head->prev = nullptr;
        delete timer;
        return;
    }
    else if (timer == tail) {
        tail = tail->prev;
        tail->next = nullptr;
        delete timer;
        return;
    }
    else {
        timer->prev->next = timer->next;
        timer->next->prev = timer->prev;
        delete timer;
    }
}


/* 定时器到期处理函数 */
void sort_timer_lst::tick() {
    if (!head) {
        return;
    }

    time_t cur = time(nullptr); /* 当前时间 */
    util_timer* tmp = head;
    while (tmp) {
        /* 当前时间小于链表当前节点定时器时间 */
        if (cur < tmp->expire) {
            break;
        }
        /* 否则说明有定时器到期，执行回调函数 */
        tmp->cb_func(tmp->user_data);
        head = tmp->next;
        if (head) {
            head->prev = nullptr;
        }
        delete tmp;
        tmp = head;
    }
}

void sort_timer_lst::add_timer(util_timer* timer, util_timer* lst_head) {
    util_timer* prev = lst_head;
    util_timer* tmp = lst_head->next;
    while (tmp) {
        if (timer->expire < tmp->expire) {
            prev->next = timer;
            timer->prev = prev;
            timer->next = tmp;
            tmp->prev = timer;
            break;
        }
        prev = tmp;
        tmp = tmp->next;
    }
    if (!tmp) {
        prev->next = timer;
        timer->prev = prev;
        timer->next = nullptr;
        tail = timer;
    }   
}

void Utils::init(int timeslot) {
    m_TIMESLOT = timeslot;
}

/* 对文件描述符设置非阻塞 */
int Utils::setnonblocking(int fd) {
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

/* 将内核时间表注册读事件， ET模式，选择开启 EPOLLONESHOT */
void Utils::addfd(int epollfd, int fd, bool one_shot, int TRIGMode) {
    epoll_event event;
    event.data.fd = fd;

    if (TRIGMode == 1) {
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    }
    else {
        event.events = EPOLLIN | EPOLLRDHUP;
    }
    if (one_shot) {
        event.events |= EPOLLONESHOT;
    }
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

/* 信号处理函数 */
void Utils::sig_handler(int sig) {
    /* 为了保证函数的可重入性，保留原来的 errno */
    int save_errno = errno;
    int msg = sig;
    send(u_pipefd[1], (char*)&msg, 1, 0);
    errno = save_errno;
}

/* 设置信号函数 */
void Utils::addsig(int sig, void(handler)(int), bool restart) {
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    if (restart) {
        sa.sa_flags |= SA_RESTART;
    }
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, nullptr) != -1);
}

/* 定时处理任务 */
void Utils::timer_handler() {
    m_timer_lst.tick();
    alarm(m_TIMESLOT);
}

void Utils::show_error(int connfd, const char* info) {
    send(connfd, info, strlen(info), 0);
    close(connfd);
}

int *Utils::u_pipefd = 0;
int Utils::u_epollfd = 0;

class Utils;

void cb_func(client_data* user_data) {
    epoll_ctl(Utils::u_epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0);
    assert(user_data);
    close(user_data->sockfd);
    http_conn::m_user_count--;
}