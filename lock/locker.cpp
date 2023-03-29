#include "locker.h"

/*****************信号量类的成员函数实现**********************/
// 构造函数， 信号量的创建
sem::sem() {
    if (sem_init(&m_sem, 0, 0) != 0) {
        throw std::exception();
    }
}
sem::sem(int num) {
    if (sem_init(&m_sem, 0, num) != 0) {
        throw std::exception();
    }
}
// 析构函数，信号量的销毁
sem::~sem() {
    sem_destroy(&m_sem);
}

// 信号量的 P V 操作
bool sem::wait() {
    return sem_wait(&m_sem) == 0;
}
bool sem::post() {
    return sem_post(&m_sem) == 0;
}

/*****************互斥量类的成员函数实现**********************/
// 构造函数，互斥量的创建
locker::locker() {
    if (pthread_mutex_init(&m_mutex, NULL) != 0) {
        throw std::exception();
    }
}

// 析构函数，互斥量的销毁
locker::~locker() {
    pthread_mutex_destroy(&m_mutex);
}

// 上锁与解锁
bool locker::lock() {
    return pthread_mutex_lock(&m_mutex) == 0;
}
bool locker::unlock() {
    return pthread_mutex_unlock(&m_mutex) == 0;
}

/* 获取互斥锁 */
pthread_mutex_t* locker::get() {
    return &m_mutex;
}


/*****************条件变量类的成员函数实现*********************/
// 构造函数，条件变量的创建
cond::cond() {
    if (pthread_cond_init(&m_cond, NULL) != 0) {
        throw std::exception();
    }
}

// 析构函数：条件变量的销毁
cond::~cond() {
    pthread_cond_destroy(&m_cond);
}

// 阻塞当前线程
bool cond::wait(pthread_mutex_t *m_mutex) {
    int ret = 0;
	pthread_mutex_lock(m_mutex);
    ret = pthread_cond_wait(&m_cond, m_mutex);
	pthread_mutex_unlock(m_mutex);
    return ret == 0;
}
bool cond::timewait(pthread_mutex_t *m_mutex, struct timespec t) {
    int ret = 0;
    pthread_mutex_lock(m_mutex);
    ret = pthread_cond_timedwait(&m_cond, m_mutex, &t);
    pthread_mutex_unlock(m_mutex);
    return ret == 0;
}
//唤醒一个等待目标条件变量的线程，取决于线程的优先级和调度策略
bool cond::signal() {
    return pthread_cond_signal(&m_cond) == 0;
}

bool cond::broadcast() {
    return pthread_cond_broadcast(&m_cond) == 0;
}
