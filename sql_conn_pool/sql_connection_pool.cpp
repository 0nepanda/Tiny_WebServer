#include "sql_connection_pool.h"

connection_pool::connection_pool() {
    m_CurConn = 0;
    m_FreeConn = 0;
}

connection_pool* connection_pool::GetInstance() {
    static connection_pool connPool;
    return & connPool;
}

void connection_pool::init(string Url, string User, string PassWord, string DataBaseName, int Port, int MaxConn, int close_log) {
    m_url = Url;
    m_User = User;
    m_PassWord = PassWord;
    m_DataBaseName = DataBaseName;
    m_Port = Port;
    m_close_log = close_log;

    for (int i = 0; i < MaxConn; i++) {
        MYSQL* con = nullptr;
        
        con = mysql_init(con);
        if (con == nullptr) {
            LOG_ERROR("%s", "MySQL Error");
            exit(1);
        }

        con = mysql_real_connect(con, Url.c_str(), User.c_str(), PassWord.c_str(), DataBaseName.c_str(), Port, NULL, 0);
        if (con == nullptr) {
            LOG_ERROR("%s", "MySQL Error");
            exit(1);
        }
        connList.push_back(con);
        m_FreeConn++;
    }
    reserver = sem(m_FreeConn);
    m_MaxConn = m_FreeConn;
}

/* 当有请求时，从数据库连接池中返回一个可用的连接，更新使用和空闲连接数 */
MYSQL* connection_pool::GetConnection() {
    MYSQL* con = nullptr;

    if (connList.size() == 0) {
        return nullptr;
    }

    reserver.wait();
    
    lock.lock();

    con = connList.front();
    connList.pop_front();
    m_FreeConn--;
    m_CurConn++;

    lock.unlock();
    return con;
}

/* 释放当前使用的连接 */
bool connection_pool::ReleaseConnection(MYSQL* con) {
    if (con == nullptr) {
        return false;
    }

    lock.lock();

    connList.push_back(con);
    m_FreeConn++;
    m_CurConn--;

    lock.unlock();

    reserver.post();
    return true;
}

/* 销毁数据库连接池 */
void connection_pool::DestoryPool() {
    lock.lock();
    if (connList.size() > 0) {
        list<MYSQL*>::iterator it;
        for (it = connList.begin(); it != connList.end(); it++) {
            MYSQL* con = *it;
            mysql_close(con);
        }
        m_CurConn = 0;
        m_FreeConn = 0;
        connList.clear();
    }
    lock.unlock();
}

/* 当前空闲连接数 */
int connection_pool::GetFreeConn() {
    return this->m_FreeConn;
}

connection_pool::~connection_pool() {
    DestoryPool();
}

connectionRAII::connectionRAII(MYSQL** sql, connection_pool* connpool) {
    *sql = connpool->GetConnection();

    conRAII = *sql;
    poolRAII = connpool;
}

connectionRAII::~connectionRAII() {
    poolRAII->ReleaseConnection(conRAII);  
}