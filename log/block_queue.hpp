/*************************************************************/
/* 循环数组实现的阻塞队列，m_back = (m_back + 1) % m_max_size; */  
/* 线程安全，每个操作前都要先加互斥锁，操作完后，再解锁          */
/*************************************************************/

#ifndef BLOCK_QUEUE_H
#define BLOCK_QUEUE_H

#include <iostream>
#include <stdlib.h>
#include <pthread.h>
#include <sys/time.h>
#include "../lock/locker.h"
using namespace std;

template <typename T>
class block_queue {
public:
    block_queue(int max_size);
    ~block_queue();
    void clear(); /* 清空阻塞队列 */
    bool full();  /* 判断阻塞队列是否满了 */
    bool empty(); /* 判断阻塞队列是否为空 */
    bool front(T& value);  /* 返回队首元素 */
    bool back(T& value);   /* 返回队尾元素 */
    int size();  /* 返回队列的当前长度 */
    int max_size(); /* 返回队列的最大长度 */
    bool push(const T& item); /* 向队列尾部添加元素 */
    bool pop(T& item); /* 弹出队首元素 */
    bool pop(T& item, int ms_timeout); /* 重载一个处理超时的版本 */
private:
    locker m_mutex;
    cond m_cond;

    T* m_array;
    int m_size;
    int m_max_size;
    int m_front;
    int m_back;
};

template <typename T>
block_queue<T>::block_queue(int max_size) {
    if (max_size <= 0) {
        exit(-1);
    }
    m_max_size = max_size;
    m_array = new T[max_size];
    m_size = 0;
    m_front = -1;
    m_back = -1;
}

template <typename T>
block_queue<T>::~block_queue() {
    m_mutex.lock();
    if (m_array != nullptr) {
        delete[] m_array;
        m_array = nullptr;
    }
    m_mutex.unlock();
}

template <typename T>
void block_queue<T>::clear() {
    m_mutex.lock();
    m_size = 0;
    m_front = -1;
    m_back = -1;
    m_mutex.unlock();
}

template <typename T>
bool block_queue<T>::full() {
    m_mutex.lock();
    if (m_size >= m_max_size) {
        m_mutex.unlock();
        return true;
    }
    m_mutex.unlock();
    return false;
}

template <typename T>
bool block_queue<T>::empty() {
    m_mutex.lock();
    if (m_size == 0) {
        m_mutex.unlock();
        return true;
    }
    m_mutex.unlock();
    return false;
}

template <typename T>
bool block_queue<T>::front(T& value) {
    m_mutex.lock();
    if (m_size == 0) {
        m_mutex.unlock();
        return false;
    }
    value = m_array[m_front];
    m_mutex.unlock();
    return true;
}

template <typename T>
bool block_queue<T>::back(T& value) {
    m_mutex.lock();
    if (m_size == 0) {
        m_mutex.unlock();
        return false;
    }
    value = m_array[m_back];
    m_mutex.unlock();
    return true;
} 

template <typename T>
int block_queue<T>::size() {
    int tmp = 0;

    m_mutex.lock();
    tmp = m_size;
    m_mutex.unlock();

    return tmp;
}

template <typename T>
int block_queue<T>::max_size() {
    int tmp = 0;

    m_mutex.lock();
    tmp = m_max_size;
    m_mutex.unlock();

    return tmp;
}

template <typename T>
bool block_queue<T>::push(const T& item) {
    m_mutex.lock();
    if (m_size >= m_max_size) {
        m_cond.broadcast();
        m_mutex.unlock();
        return false;
    }
    /* 循环数组实现 */
    m_back = (m_back + 1) % m_max_size;
    m_array[m_back] = item;
    m_size++;
    m_cond.broadcast();
    m_mutex.unlock();
    return true;
}

template <typename T>
bool block_queue<T>::pop(T& item) {
    m_mutex.lock();
    while (m_size <= 0) {
        if (!m_cond.wait(m_mutex.get())) {
            m_mutex.unlock();
            return false;
        }
    }
    m_front = (m_front + 1) % m_max_size;
    item = m_array[m_front];
    m_size--;
    m_mutex.unlock();
    return true;
}

/* 重载超时版本 */
template <typename T>
bool block_queue<T>::pop(T& item, int ms_timeout) {
    struct timespec t = {0, 0};
    struct timeval now = {0, 0};
    gettimeofday(&now, NULL);
    m_mutex.lock();
    if (m_size <= 0) {
        t.tv_sec = now.tv_sec + ms_timeout / 1000;
        t.tv_nsec = (ms_timeout % 1000) * 1000;
        if (!m_cond.timewait(m_mutex.get(), t)) {
            m_mutex.unlock();
            return false;
        }
    }

    if (m_size <= 0) {
        m_mutex.unlock();
        return false;
    }

    m_front = (m_front + 1) % m_max_size;
    item = m_array[m_front];
    m_size--;
    m_mutex.unlock();
    return true;
}

#endif