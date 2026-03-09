#include <stdio.h>          // 标准输入输出库
#include <stdlib.h>         // 标准库，包含malloc、free等函数
#include <string.h>         // 字符串处理库
#include <unistd.h>         // 系统调用库，包含read、write、close等函数
#include <pthread.h>        // 线程库
#include <sys/socket.h>     // 套接字库
#include <netinet/in.h>     // 网络地址结构库
#include <arpa/inet.h>      // 网络地址转换库
#include <errno.h>          // 错误处理库

#define SERVER_PORT 8888    // 服务器端口号，选择8888是因为它是一个常用的非特权端口
#define BUFFER_SIZE 1024    // 缓冲区大小，1024字节足够处理一般的请求和响应
#define DELAY_MS 1000         // 模拟网易云接口处理耗时（毫秒），用于模拟实际服务的处理时间

// 全局变量：统计当前线程数
int g_thread_count = 0;
pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER; // 互斥锁，用于保护g_thread_count的并发访问

// 线程函数：处理单个客户端连接
void *client_handler(void *arg) {
    int client_fd = *(int *)arg; // 从参数中获取客户端文件描述符
    free(arg); // 释放传递过来的内存，避免内存泄漏

    // 统计线程数
    pthread_mutex_lock(&g_mutex); // 加锁，保护g_thread_count
    g_thread_count++; // 增加线程计数
    printf("[服务器] 新连接 | 线程数：%d | 客户端FD：%d\n", g_thread_count, client_fd);
    pthread_mutex_unlock(&g_mutex); // 解锁

    char buffer[BUFFER_SIZE] = {0}; // 初始化接收缓冲区
    ssize_t n = read(client_fd, buffer, BUFFER_SIZE - 1); // 读取客户端数据，BUFFER_SIZE-1确保有空间存储终止符
    if (n <= 0) { // 读取失败或连接关闭
        goto cleanup; // 跳转到清理代码
    }
    buffer[n] = '\0'; // 添加字符串终止符

    // 模拟接口处理耗时（阻塞操作，核心缺点）
    usleep(DELAY_MS * 1000); // 转换为微秒，模拟处理延迟

    // 组装响应
    char response[BUFFER_SIZE] = {0}; // 初始化响应缓冲区
    snprintf(response, sizeof(response), "查询歌曲ID：%s | 响应：周杰伦-七里香\n", buffer); // 格式化响应内容
    write(client_fd, response, strlen(response)); // 发送响应给客户端

cleanup: // 清理标签，用于统一处理资源释放
    close(client_fd); // 关闭客户端连接
    pthread_mutex_lock(&g_mutex); // 加锁，保护g_thread_count
    g_thread_count--; // 减少线程计数
    pthread_mutex_unlock(&g_mutex); // 解锁

    pthread_exit(NULL); // 线程退出
}

/*
总结一下流程：
1、创建socket_fd套接字文件描述符(才用TCP和IPV4)
2、允许端口复用(多个进程绑定到同一个端口)
3、绑定IP和端口到socket_fd套接字文件描述符
4、listen监听
5、while
    accept 接受：从半连接队列(已经完成TCP三次连接)中取出一个连接，返回一个新的套接字文件描述符client_fd用于与客户端通信。
    pthread_create创建子线程
	{
		read读取、write写入
	}
	
*/

int main() {
    // 1. 创建TCP Socket
    // AF_INET：使用IPv4地址族
    // SOCK_STREAM：使用TCP协议
    // 0：使用默认协议
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) { // 创建失败
        perror("socket 创建失败"); // 打印错误信息
        exit(EXIT_FAILURE); // 退出程序
    }

    // 2. 端口复用
    int opt = 1; // 开启选项
    // SOL_SOCKET：套接字级别
    // SO_REUSEADDR：允许端口复用
    // SO_REUSEPORT：允许多个进程绑定到同一个端口
    // &opt：选项值的地址
    // sizeof(opt)：选项值的大小
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt));

    // 3. 绑定IP和端口
    struct sockaddr_in server_addr; // 服务器地址结构
    memset(&server_addr, 0, sizeof(server_addr)); // 初始化地址结构为0
    server_addr.sin_family = AF_INET; // 使用IPv4地址族
    server_addr.sin_addr.s_addr = INADDR_ANY; // 绑定到所有可用的网络接口
    server_addr.sin_port = htons(SERVER_PORT); // 将端口号转换为网络字节序
    // 绑定服务器地址到套接字
    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind 失败"); // 打印错误信息
        close(server_fd); // 关闭套接字
        exit(EXIT_FAILURE); // 退出程序
    }

    // 4. 监听
    // 1024：最大连接队列长度，listen 函数的第二个参数 1024 并不是限制服务器最多只能连接 1024 个客户端，而是设置 TCP 连接的半连接队列长度 。
    //半连接队列 指的是已经完成 TCP 三次握手但还没有被应用程序 accept() 函数处理的连接
    if (listen(server_fd, 1024) < 0) {
        perror("listen 失败"); // 打印错误信息
        close(server_fd); // 关闭套接字
        exit(EXIT_FAILURE); // 退出程序
    }
    printf("[服务器] 启动成功：0.0.0.0:%d\n", SERVER_PORT); // 打印服务器启动信息
    printf("[服务器] 每连接创建一个线程，高并发下会资源耗尽！\n"); // 提示该实现的缺点

    // 5. 循环接受连接
    while (1) { // 无限循环，持续接受连接
        struct sockaddr_in client_addr; // 客户端地址结构
        socklen_t client_len = sizeof(client_addr); // 客户端地址长度
        int *client_fd = malloc(sizeof(int)); // 为客户端文件描述符分配内存
        // 接受客户端连接
        // server_fd：服务器套接字
        // &client_addr：客户端地址结构
        // &client_len：客户端地址长度
        *client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (*client_fd < 0) { // 接受失败
            perror("accept 失败"); // 打印错误信息
            free(client_fd); // 释放内存
            continue; // 继续下一次循环
        }

        // 6. 创建线程处理连接
        pthread_t tid; // 线程ID
        // 创建线程
        // &tid：线程ID
        // NULL：使用默认线程属性
        // client_handler：线程函数
        // client_fd：传递给线程函数的参数
        if (pthread_create(&tid, NULL, client_handler, client_fd) != 0) {
            perror("pthread_create 失败"); // 打印错误信息
            close(*client_fd); // 关闭客户端连接
            free(client_fd); // 释放内存
            continue; // 继续下一次循环
        }
        pthread_detach(tid); // 分离线程，自动释放资源，不需要主线程调用 pthread_join，避免僵尸线程
    }

    close(server_fd); // 关闭服务器套接字（理论上不会执行到这里）
    return 0; // 返回0
}