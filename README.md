## myTinyWebServer

#### 编译前的准备工作：

需要准备好 CenOS 下 C++ 连接 MySQL 配置，我的CSDN博客中有相关介绍，给出链接：

https://blog.csdn.net/Yetao1996/article/details/124745730?spm=1001.2014.3001.5501

​		在MySQL数据库中创建数据库 webserver，创建表 user，含 username 和 passwd 两个字段，可自行命名，但是要在代码中相应位置做修改。 在 main.cpp 中修改你的 MySQL 账号密码！关于 MySQL 还有可能需要修改 makefile 文件中的路径！

#### 项目概述：

参考游双老师的《Linux高性能服务器编程》以及Github上的一些现有资源，本项目实现了一个Linux下的轻量级HTTP服务器

HTTP服务器的大体流程为：监听  ---->  连接  ---->  读HTTP请求  ---->  解析HTTP请求  ---->  写 HTTP 响应

- 本项目中在主线程使用 epoll （实现LT 和 ET 两个模式）进行监听
- 使用有限状态机来解析HTTP请求报文，支持解析GET请求和POST请求两种
- 实现 Reactor(读写由工作线程完成) 和 Proactor(读写由主线程完成)两种事件处理模式
- 使用 线程池 来处理 读写 + HTTP 解析等操作(Reactor) 或 HTTP 解析等操作(Proactor)
- 实现同步/异步两种日志写入方式，在异步中使用一个阻塞队列来作为缓冲
- 使用MySQL数据库来实现登录和注册功能，登录之后可以请求服务器中的图片或视频等资源

#### 框架：

![框架](https://github.com/yetao1121/myTinyWebServer/blob/main/root/framework.jpg?raw=true)

#### 实现效果：
登陆注册：

![注册登录](https://github.com/yetao1121/myTinyWebServer/blob/main/root/login_register.gif)

访问图片视频：
![访问图片视频](https://raw.githubusercontent.com/yetao1121/myTinyWebServer/main/root/visit_picture_vedio.gif)

此间发生的日志记录：

![日志](https://github.com/yetao1121/myTinyWebServer/blob/main/root/log.jpg)

#### 最后

此外，本项目在各个模块文件夹内都附有超详细笔记，在代码内也附有超详细注释，保证所有人都可以很清晰明了！

非常感谢：

《TCP/IP网络编程》-韩-尹圣雨

《Linux高性能服务器编程》 - 游双

https://github.com/qinguoyi/TinyWebServer
