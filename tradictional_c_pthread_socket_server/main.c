#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

#define SERVER_PORT 8888
#define BUFFER_SIZE 1024
#define DELAY_MS 10       // 模拟网易云接口处理耗时（毫秒）

// 全局变量：统计当前线程数
int g_thread_count = 0;
pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;

// 线程函数：处理单个客户端连接
void *client_handler(void *arg) {
    int client_fd = *(int *)arg;
    free(arg);

    // 统计线程数
    pthread_mutex_lock(&g_mutex);
    g_thread_count++;
    printf("[服务器] 新连接 | 线程数：%d | 客户端FD：%d\n", g_thread_count, client_fd);
    pthread_mutex_unlock(&g_mutex);

    char buffer[BUFFER_SIZE] = {0};
    ssize_t n = read(client_fd, buffer, BUFFER_SIZE - 1);
    if (n <= 0) {
        goto cleanup;
    }
    buffer[n] = '\0';

    // 模拟接口处理耗时（阻塞操作，核心缺点）
    usleep(DELAY_MS * 1000);

    // 组装响应
    char response[BUFFER_SIZE] = {0};
    snprintf(response, sizeof(response), "查询歌曲ID：%s | 响应：周杰伦-七里香\n", buffer);
    write(client_fd, response, strlen(response));

cleanup://C++ 严格禁止 goto 跳过变量的初始化（C 语言允许）
    close(client_fd);
    pthread_mutex_lock(&g_mutex);
    g_thread_count--;
    pthread_mutex_unlock(&g_mutex);

    pthread_exit(NULL);
}

int main() {
    // 1. 创建TCP Socket
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket 创建失败");
        exit(EXIT_FAILURE);
    }

    // 2. 端口复用
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt));

    // 3. 绑定IP和端口
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(SERVER_PORT);
    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind 失败");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    // 4. 监听
    if (listen(server_fd, 1024) < 0) {
        perror("listen 失败");
        close(server_fd);
        exit(EXIT_FAILURE);
    }
    printf("[服务器] 启动成功：0.0.0.0:%d\n", SERVER_PORT);
    printf("[服务器] 每连接创建一个线程，高并发下会资源耗尽！\n");

    // 5. 循环接受连接
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int *client_fd = malloc(sizeof(int));
        *client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (*client_fd < 0) {
            perror("accept 失败");
            free(client_fd);
            continue;
        }

        // 6. 创建线程处理连接
        pthread_t tid;
        if (pthread_create(&tid, NULL, client_handler, client_fd) != 0) {
            perror("pthread_create 失败");
            close(*client_fd);
            free(client_fd);
            continue;
        }
        pthread_detach(tid); // 分离线程，自动释放资源
    }

    close(server_fd);
    return 0;
}