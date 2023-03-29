## myTinyWebServer 运行流程

### 主线程运行流程：

1、初始化数据库信息，登录名，密码，库名

2、解析命令行：

```
 -p 端口号，默认值为 9006
 -l 日志写入方式，默认为0，同步写入；可设置为1，异步写入；
 -m 触发组合模式，默认为 0 ：LT + LT, 可设置为 1：LT + ET；2：ET + LT；3：ET + ET
 -o 优雅关闭连接， 默认为 0，不启用， 可设置为 1：启用
 -s 数据库数量，默认为 8
 -t 线程池内线程数 默认为 8
 -c 是否关闭日志，默认为 0，不关闭，可修改为 1 关闭日志
 -a 选择并发模型 默认为 0，Proactor， 可修改为 1 Reactor
```

3、创建 WebServer 类， 使用上面的数据进行初始化

4、运行日志，WebServer::log_write()，此函数内如按照如下初始化日志类，日志初始化之后，可以调用四个宏函数写入信息进日志文件

```c++
根据标志位，如果开启日志且写入方式为异步：
    Log::get_instance()->init("./ServerLog", m_close_log,    2000,      800000,      800);
								 日志文件名      关闭日志    日志缓冲区大小   最大行数   阻塞队列大大长度
如果开启日志且写入方式为同步：
	Log::get_instance()->init("./ServerLog", m_close_log, 2000, 800000, 0);

```

5、运行数据库：WebServer::sql_pool(), 此函数内部先创建数据库连接池实例，使用第三步初始化的内容初始化数据库连接池，创建m_sql_num个数据库连接存入 connList 连接池中，然后初始化一个数据库的读取表放入 http 类的 map 中

```
void WebServer::sql_pool() {
    /* 初始化数据库连接池 */
    m_connPool = connection_pool::GetInstance();
    m_connPool->init("localhost", m_user, m_passWord, m_dataBaseName, 3306, m_sql_num, m_close_log);
    /* 初始化数据库读取表 */
    users->initmysql_result(m_connPool);
}
```

6、运行线程池：WebServer::thread_pool(), 创建一个线程池，threadpool构造函数创建m_thread_num个线程放入m_threads 数组中

```
void WebServer::thread_pool() {
    /* 创建线程池 */
    m_pool = new threadpool<http_conn>(m_actormodel, m_connPool, m_thread_num);
}
```

7、触发模式：WebServer::trig(),根据第三步的m_TRIGMode值，来更新WebServer类的 m_LISTENTrigmode   m_CONNTrigmode 值

8、启动监听：WebServer::eventLisen()

- ```
  socket 创建服务端套接字 m_listenfd
  ```

- ```
  根据第三步初始化的 opt_linger 值选择是否设置优雅的关闭连接 
  ```

- ```
  bind 绑定 ip 地址，端口号
  ```

- ```
  开启监听 listen
  ```

- ```
  初始化服务器定时器(成员变量)的超时参数 timeslot
  ```

- ```
  创建 epoll、epoll 内核事件表，并将服务端套接字加到epoll中监听，同时初始化 http 类的静态成员变量 m_epollfd
  ```

- ```
  创建一对相互连接的文件描述符 m_pipefd[0] m_pipefd[1],将 m_pipefd[1]设置为非阻塞IO，将 m_pipefd 添加至 epoll
  ```

- ```
  注册信号 SIGPIPE,回调函数SIG_IGN（忽略SIGPIPE信号），注册信号 SIGALRM 和 SIGTERM，回调函数均为 utils.sig_handler 
  sig_handler内容为 	向 m_pipefd[1] 写入触发的信号值
  ```

- ```
  声明一个 TIMESLOT 的 alarm（这个信号到时间就会触发处理定时过期任务的函数），初始化 Utils 类的静态成员变量 u_pipefd, u_epollfd	
  ```

9、运行：WebServer::eventLoop()，当  stop_server 标志位不为真时执行以下循环：

- ```
  int number = epoll_wait(m_epollfd, events, MAX_EVENT_NUMBER, -1);  //开始等待客户端的连接
  如果 epoll_wait 返回错误，向日志记录错误信息，结束循环
  ```

- ```
  epoll_wait 正确返回 number，循环 number 次处理这 number 个请求
  
  	如果是新到的客户端连接(connsockfd == m_listenfd),调用 dealclientdata 函数处理此连接，
  		dealclientdata函数内：根据不同的 m_LISTENTrigmode 值，使用不同的方式创建连接，使用accept接收该连接connfd，使		用users[connfd].init()初始化该http连接：（初始化客户sockfd，地址，m_user_count++, 网站根目录，触发模式，是		否开启日志，以及数据库的用户名、密码、库名，再调用init()初始化一些参数，
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
          memset(m_real_file, '\0', FILENAME_LEN);）
          
          初始化完http连接，再初始化定时器相关参数：
          users_timer[connfd].address = client_address;   /* 客户端地址 */
          users_timer[connfd].sockfd = connfd;            /* 客户端文件描述符 */
          util_timer* timer = new util_timer;             /* 创建一个定时器 */
          timer->user_data = &users_timer[connfd];        /* 定时器的连接资源为刚连接的客户端 */
          timer->cb_func = cb_func;                       /* 设置定时器的回调函数 */
          time_t cur = time(nullptr);                     /* 记录当前时间 */
          timer->expire = cur + 3 * TIMESLOT;             /* 将此定时器的超时时间设置为 当前时间+3*TIMESLOT */
          users_timer[connfd].timer = timer;              /* 设置当前客户端的定时器为刚设置好的定时器 */
          utils.m_timer_lst.add_timer(timer);             /* 将此定时器添加到升序链表中 */
          这样便完成对该客户端的连接，接下来等待该用户的请求，或超时关闭该连接
      
      如果不是新的请求，而是 EPOLLRDHUP 或 EPOLLHUP 或 EPOLLERR 事件，调用 deal_timer 函数，传入对应响应的定时器 		timer和文件描述符fsockd，函数内：
      	调用timer->cb_func(),该函数：删除epoll上对应的sockfd，关闭该客户端sockfd，将客户链接数m_user_count减一
      	从升序链表中删除该定时器
      	将关闭信息写入日志文件
      	
      如果 (sockfd == m_pipefd[0]) && (events[i].events & EPOLLIN)，说明是信号回调函数从m_pipefd[0]将触发的信号写 	给 m_pipefd[1]，此时调用 dealwithsignal函数，传入timeout, stop_server，该函数内：
      	接收 m_pipefd[0] 传过来的数据，判断是否为 SIGALRM 信号 或者 SIGTERM 信号，如果是，把对应的 timeout, 				stop_server 修改为真，返回。
      	如果 timeout 为真， 调用utils.timer_handler(); 将 time tick 写入日志， timer_handler函数内：
      		调用m_timer_list.tick()函数，该函数内判断是否有任务到期，如果有，调用cb_func处理定时到期任务，
      	如果 stop_server 为真，结束程序。
      
      如果不是以上情况，并且sock触发的是一个读事件，即events[i].events & EPOLLIN，调用 dealwithread(sockfd) 函数：
      	获取该连接资源对应的定时器，根据 m_actormodel 的值来处理读事件：
      	如果是Reactor模式：
      		将定时器延时
      		将该 http 读事件（users + sockfd）放进线程池的请求队列中，读写标志位设置为 0，由工作线程来处理该读事件
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
              improv 和 timer_flag 仅在 Reactor 模式下有用，具体解析如下：
              	Reactor模式写主线程只负责监听，读写也是在工作线程中进行的，那么工作线程处理完读写事件就要通知主线程
              	而 improv 和 timer_flag 就起到这个作用，improv 标志读写完毕，time_flag 标志是否关闭连接
              	主线程在Reactor模式下，一直等待 improv 位变为 1，标志着工作线程读写完毕，下面的写事件也是同理	
      	如果是Proactor模式：
      		主线程调用 users[sockfd].read()函数，该函数使用 LT 或者 ET 模式将输入缓冲中的数据读入 m_read_buf 中，并			修改成员变量 m_read_idx（已经读入的数据的最后一个字节的下一个位置）
      		读完之后将该任务交给工作线程，调用append_p，即放入线程池的请求队列
      		将定时器延时
      		如果users[sockfd].read()函数返回假，说明客户端断开连接，调用deal_timer函数关闭该连接
      
      如果不是以上情况，并且sock触发的是一个写事件，即events[i].events & EPOLLOUT，调用dealwithwrite(sockfd)函数：
      	获取该连接资源对应的定时器，根据 m_actormodel 的值来处理读事件：
      	如果是Reactor模式：
      		将定时器延时
      		将该 http 写事件（users + sockfd）放进线程池的请求队列中，读写标志位设置为 1，由工作线程来处理该写事件
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
          如果是Proactor模式：
          	主线程调用users[sockfd].write()函数，！！！！！！！！ 这个函数要再看看
          	将定时器延时
          	如果返回假，调用deal_timer关闭该连接
  
  主线程将一直循环执行以上操作！！！
  ```
  

### 工作线程执行流程

主线程运行的第六步为创建一个线程池（此时已经为这个线程池分配了一个数据库连接池，这样工作线程可以取数据库连接池用）：

```
void WebServer::thread_pool() {
    /* 创建线程池 */
    m_pool = new threadpool<http_conn>(m_actormodel, m_connPool, m_thread_num);
}
```

```
threadpool()构造函数内容如下：
	如果 输入的线程数或最大请求数小于等于0，抛异常退出
	创建 thread_number 个线程，并将他们都设置为脱离线程，放在m_threads数组中（只是把线程id存在这里）
		其中创建每个线程：pthread_create(m_threads + i, nullptr, worker, this)时，
		使其运行 worker 函数，传入线程池类的 this 指针
```

```
此后每个线程便运行 worker 函数：
	worker函数取出线程池并返回，同时调用 run 函数，也即意味着每个线程在 worker 这没干啥就转去运行 run() 函数
```

```
run()函数：
	while(true)，执行以下内容，也就是，一直执行以下内容：
		使用信号量 m_queuestate.wait(); 从请求队列中取出一个任务，如果请求队列中没有任务，阻塞当前线程等待被唤醒
		否则上锁从请求队列中取出一个任务 request（http类） 来，因为请求队列被所有线程共享，所有一定要上锁
		如果是 Reactor 模型：
			判断 request->m_state 标志位来决定调用读函数还是写函数；如果是读事件，在工作线程执行完读操作之后，使用RAII机制			  给该 reques 分配一个数据库连接，赋给 http 类的成员变量 MYSQL* mysql 上， 调用 process() 函数
			如果是写事件，写完将 improv 置 1 结束， 继续执行循环，从循环队列中取任务
		如果是 Proactor 模型：
			说明读写已经在主线程中完成了，为该 request分配一个数据库连接，直接调用process函数
```

```
process函数：
	先调用process_read()函数解析HTTP请求，并返回解析结果。process_read()使用有限状态机解析HTTP请求，主状态机如下：
		首先使用 parse_line 解析出HTTP请求中的一行数据，将其修改为以'\0'结尾
		然后使用 get_line() 函数取出刚刚解析的一行数据，主状态机根据 m_check_state 的状态来决定如何处理这行数据
			如果m_check_state当前为CHECK_STATE_REQUESTLINE，调用parse_request_line函数来得到请求行相关参数
			如果m_check_state当前为CHECK_STATE_HEADER，调用parse_headers函数来得到请求头相关参数
			如果m_check_state当前为CHECK_STATE_CONTENT，调用parse_content函数来得到请求体的内容
			如果parse_headers或者parse_content返回GET_REQUEST，说明解析HTTP请求已经完成，执行do_request函数
```

```
parse_line函数：
	根据每一行数据都以 \r\n 结尾这一特点来解析 HTTP 请求， 依次检查 m_read_buf 中的每一个字符，检查到连着的 \r\n 将他们修改 	为 \0(此时 m_check_idx 指向下一行的开头)，返回LINE_OK, 	检测到 \r或\n旁边不是对方，返回LINE_BAD， 检测到\r结尾，或	 者没有\r\n，返回LINE_OPEN
```

```
get_line函数：
	char* get_line() {
        return m_read_buf + m_start_line;  /* m_start_line初始为0，之后每次为m_check_idx起始位置*/
    }
```

```
parse_request_line函数：
	根据 " \t" 空格和反斜杠t来解析 请求方法、目标 URL，以及 HTTP 版本号，涉及以下几个库函数：
		strpbrk(char* text, char* " \t") /* 返回两个字符串中首个相同字符的位置，即text中第一次出现空格或者\t的位置 */
		strcasecmp(char* method, "GET")  /* 比较两个字符串是否相等，忽略大小写！！！相等返回 0 */
		strspn(m_url, " \t")  /* 检索 m_url 中第一个不在第二个字符串中出现的字符下标。即如果后面还有空格，跳过这些空格*/
		strncasecmp(m_url, "http://", 7)，与strcasecmp类似，但是增加了比较的位数
		strlen(m_url) /* 返回字符串长度 */
		strcat(m_url, "judge.html")  /* 在目标字符串后追加第二个字符串 */
```

```
parse_headers函数：
	请求头部以一个空行结束，所以当解析的某行为空行时，表示解析请求头结束，此时关注 m_content_length， 这个量会在解析空行之前被  	解析到，如果 m_content_length 不为 0，说明是POST请求，请求体内还有内容，将状态机改为 CHECK_STATE_CONTENT，如果		m_content_length = 0，说明是 GET 请求，直接返回 GET_REQUEST 完成解析
	如果不是空格，使用 strncasecmp 函数比较前相应位数是不是：Connection: Content-Length: Host:，如果是的话，完成相应解	析，如果不是 向日志记录 LOG_INFO("oop! unknow header %s", text);
```

```
parse_content函数：
	将 content 中的内容整体赋值给 m_string 成员变量即可，不用做过多的解析
```

```
do_request函数：
	执行do_request函数时，说明我们已经得到了一个完整的HTTP请求，此时开始分析目标文件的属性。如果目标文件存在、且对所以用户可		读，且不是目录，就使用 mmap 将其映射到内存地址 m_file_address 处，并告诉调用者获取文件成功
	首先定义一个char*指针指向最后一个'/'处，const char* p = strrchr(m_url, '/'); 然后根据*(p + 1)的值来跳转页面，这个值	是由 相应 html 文件的 action 标签提供的
	
	最开始访问 judge.html,有新用户和已有账号两个按钮，点击新用户按钮action属性为0， 已有账号action属性为1
	do_request函数中 根据*(p + 1)跳转，如果是0，将 m_real_file 设置为 register.html 的路径，如果是 1，设置 log.html
	
	如果当前在register.html或者log.html页面，填写用户名、密码，点击注册或登录，浏览器会向服务器发送POST请求，请求体内包含有	用户名和密码，同时 action 属性设置为 3或2，如果*(p + 1) = 3 or 2, 先将 m_string(请求体)内 name和password解析出来，
	如果是注册，判断当前map中有没有重复的，没有的话向mysql输入一条插入语句，同时添加进map（这个过程需要加锁），然后将			m_real_file路径设置为log.html（注册成功去登录）,如果有的话将m_realfile路径设置为registerError.html。
	如果是登录的话，直接判断当前map中有没有，如果有将m_real_file路径设置为welcome.html,没有设置为logError.html路径。
	
	其余页面条也是类似的操作，不再赘述。
	
	根据请求结果，设置好 m_real_file 的值之后，使用stat函数来查看 m_real_file 这个路径上的文件属性：
		stat(m_real_file, &m_file_stat) 返回小于0，说明没找到文件，返回 NO_RESOURCE
		if (!(m_file_stat.st_mode & S_IROTH))，说明不具有可读权限，返回FORBIDDEN_REQUEST
		if (S_ISDIR(m_file_stat.st_mode))，说明为目录，返回BAD_REQUEST
		否则将该文件用mmap映射到内存上，返回 FILE_REQUEST
```

```
在process_read()函数执行完毕之后，如果返回请求不完整，重置epoll事件等待客户端接下来的请求，返回；
否则执行 process_write()函数，根据process_read()返回的 HTTPCODE 来给客户端写请求：
	如果process_read()返回INTERNAL_ERROR，向 m_write_buf 添加错误码 500 相关的响应行，响应头，响应体，跳出switch
	如果process_read()返回BAD_REQUEST，向 m_write_buf 添加错误码 400 相关的响应行，响应头，响应体，跳出switch
	如果process_read()返回NO_RESOURCE，向 m_write_buf 添加错误码 404 相关的响应行，响应头，响应体，跳出switch
	如果process_read()返回FORBIDDEN_REQUEST，向 m_write_buf 添加错误码 403 相关的响应行，响应头，响应体，跳出switch
	如果process_read()返回FILE_REQUEST，向 m_write_buf 添加 200 ok响应行
		如果文件大小不为零，向m_write_buf 添加响应头，然后将m_iv[0]指向m_write_buf,m_iv[1]指向m_real_address			   （do_request函数内使用mmap函数映射的地址），更新m_iv_count=2，bytes_to_send = m_write_buf大小加上文件大小
		需要说明的是，除了FILE_REQUEST外，其他状态只需要返回m_write_buf中的内容，所以m_iv只用1个就行，指向m_write_buf
		
		指定好需要返回给客户的数据地址及大小（FILE_REQUEST下分别存放在m_write_buf和m_real_address中），使用modfd函数做如		 下操作：重置EPOLL事件，添加EPOLLOUT事件。
		此时主线程：epollLoop函数中epoll_wait函数接触发EPOLLOUT事件，调用dealwithwrite处理写事件，如上面所示！
```

