target=myTinyWebserver
libs=main.cpp ./config/config.cpp ./http/http_conn.cpp ./lock/locker.cpp ./log/log.cpp ./sql_conn_pool/sql_connection_pool.cpp ./threadpool/threadpool.hpp ./timer/lst_timer.cpp ./WebServer/WebServer.cpp

$(target):$(libs)
	$(CXX) -std=c++11 -I/usr/include/mysql -L/usr/lib64/mysql $^ -o $@ -lpthread -lmysqlclient

clean:
	rm -f myTinyWebserver
