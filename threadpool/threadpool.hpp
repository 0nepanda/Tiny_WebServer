#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <cstdio>
#include <list>
#include <exception>
#include <pthread.h>

#include "../lock/locker.h"
#include "../sql_conn_pool/sql_connection_pool.h"

template <typename T>
class threadpool {
public:
    threadpool(int actor_model, connection_pool* connPool, int thread_number = 8, int max_requests = 10000);
    ~threadpool();
    bool append(T* request, int state);
    bool append_p(T* request);

private:
    static void* worker(void* arg);  /* 工作线程运行的函数，它不断从工作队列中取出任务并执行*/
    void run();

private:
    int m_thread_number;         /* 线程池中的线程数 */
    int m_max_requests;          /* 请求队列中允许的最大请求数 */
    pthread_t* m_threads;        /* 描述线程池的数组，其大小为 m_thread_number */
    std::list<T*> m_workqueue;   /* 请求队列 */
    locker m_queuelocker;        /* 保护请求队列的互斥锁 */
    sem m_queuestate;            /* 是否有任务需要处理 */
    connection_pool* m_connPool; /* 数据库 */
    int m_acrot_model;           /* 模型切换 */
};

template <typename T>
threadpool<T>::threadpool(int actor_model, connection_pool* connPool, int thread_number, int max_requests)
                         : m_acrot_model(actor_model), m_thread_number(thread_number), m_max_requests(max_requests), 
                         m_threads(nullptr), m_connPool(connPool) {
    if (thread_number <= 0 || max_requests <= 0) {
        throw std::exception();
    }

    m_threads = new pthread_t[thread_number];
    if (!m_threads) {
        throw std::exception();
    }

    /* 创建 thread_number 个线程，并将他们都设置为脱离线程 */
    for (int i = 0; i < thread_number; i++) {
        if (pthread_create(m_threads + i, nullptr, worker, this) != 0) {
            delete[] m_threads;
            throw std::exception();
        }
        if (pthread_detach(m_threads[i])) {
            delete[] m_threads;
            throw std::exception();;
        }
    }
}

template <typename T>
threadpool<T>::~threadpool() {
    delete[] m_threads;
}

template<typename T>
bool threadpool<T>::append(T* request, int state) {
    /* 操作工作队列时一定要加锁，因为它被所有线程共享 */
    m_queuelocker.lock();
    if (m_workqueue.size() >= m_max_requests) {
        m_queuelocker.unlock();
        return false;
    }
    request->m_state = state;   /* 判断读写位 */
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestate.post();
    return true;
}

template<typename T>
bool threadpool<T>::append_p(T* request) {
    /* 操作工作队列时一定要加锁，因为它被所有线程共享 */
    m_queuelocker.lock();
    if (m_workqueue.size() >= m_max_requests) {
        m_queuelocker.unlock();
        return false;
    }
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestate.post();
    return true;
}

template <typename T>
void* threadpool<T>::worker(void* arg) {
    threadpool* pool = (threadpool*) arg;
    pool->run();
    return pool;
}

template <typename T>
void threadpool<T>::run() {
    while (true) {
        /* 从请求队列中取出一个 http 连接任务 */
        m_queuestate.wait();
        m_queuelocker.lock();
        if (m_workqueue.empty()) {
            m_queuelocker.unlock();
            continue;
        }
        T* request = m_workqueue.front();
        m_workqueue.pop_front();
        m_queuelocker.unlock();
        if (!request) {
            continue;
        }

        /* reactor 模型, 读写在工作线程中执行 */
        if (m_acrot_model == 1) {
            /* 读 */
            if (request->m_state == 0) {
                if (request->read()) {
                    request->improv = 1;
                    connectionRAII mysqlcon(&request->mysql, m_connPool);
                    request->process();
                }
                else {
                    request->improv = 1;
                    request->timer_flag = 1;
                }
            }
            /* 写 */
            else {
                if (request->write()) {
                    request->improv = 1;
                }
                else {
                    request->improv = 1;
                }
            }
        }
        else {
            connectionRAII mysqlcon(&request->mysql, m_connPool);
            request->process();
        }
    }
}
#endif