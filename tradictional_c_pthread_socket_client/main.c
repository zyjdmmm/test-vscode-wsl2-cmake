#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <sys/time.h>

// 全局统计变量
int g_success_count = 0;   // 成功请求数
int g_fail_count = 0;      // 失败请求数
long long g_total_delay = 0; // 总响应延迟（微秒）
pthread_mutex_t g_stat_mutex = PTHREAD_MUTEX_INITIALIZER;

// 配置参数
#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 8888
#define REQUEST_DATA "186016" // 要发送的歌曲ID

// 获取当前时间（微秒），用于计算延迟
static long long get_current_us() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000000 + tv.tv_usec;
}

// 单个客户端的请求逻辑（线程函数）
static void* client_request(void* arg) {
    (void)arg; // 未使用的参数，避免编译警告

    // 1. 创建TCP Socket
    int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        pthread_mutex_lock(&g_stat_mutex);
        g_fail_count++;
        pthread_mutex_unlock(&g_stat_mutex);
        return NULL;
    }

    // 2. 配置服务器地址
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    if (inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr) <= 0) {
        pthread_mutex_lock(&g_stat_mutex);
        g_fail_count++;
        pthread_mutex_unlock(&g_stat_mutex);
        close(sock_fd);
        return NULL;
    }

    // 记录请求开始时间
    long long start_time = get_current_us();

    // 3. 连接服务器（阻塞）
    if (connect(sock_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        pthread_mutex_lock(&g_stat_mutex);
        g_fail_count++;
        pthread_mutex_unlock(&g_stat_mutex);
        close(sock_fd);
        return NULL;
    }

    // 4. 发送请求数据
    if (send(sock_fd, REQUEST_DATA, strlen(REQUEST_DATA), 0) < 0) {
        pthread_mutex_lock(&g_stat_mutex);
        g_fail_count++;
        pthread_mutex_unlock(&g_stat_mutex);
        close(sock_fd);
        return NULL;
    }

    // 5. 接收响应
    char buffer[1024] = {0};
    ssize_t recv_len = recv(sock_fd, buffer, sizeof(buffer)-1, 0);
    if (recv_len < 0) {
        pthread_mutex_lock(&g_stat_mutex);
        g_fail_count++;
        pthread_mutex_unlock(&g_stat_mutex);
        close(sock_fd);
        return NULL;
    }

    // 6. 统计成功请求和延迟
    long long end_time = get_current_us();
    long long delay = end_time - start_time;
    
    pthread_mutex_lock(&g_stat_mutex);
    g_success_count++;
    g_total_delay += delay;
    pthread_mutex_unlock(&g_stat_mutex);

    // 7. 关闭连接
    close(sock_fd);
    return NULL;
}


/*
测试方法：/mnt/d/myfile/myproject/test_project/test-vscode-wsl2-cmake$ ./build/tradictional_c_pthread_socket_client/tradictional_c_pthread_socket_client 14000
结果：测试到14000开始不稳定有链接丢失了
传统socket服务器性能低原因：
1、查询ulimit -u发现一个进程只能创建15393个线程，服务器线程过多创建失败(可能的原因)
2、线程切换耗时90%：每次上下文切换约需要 1~10 微秒，14000 个线程在 16 核 CPU 上，
每秒会发生百万次切换,此时 CPU 100% 负载，但90% 以上的时间都在切换线程，只有不到 10% 的时间在处理实际业务
3、服务器线程中存在多个阻塞操作，这些操作让线程 “占着资源不干活”（根本原因）：
read()/recv()：等待客户端发数据时、usleep(DELAY_MS)：模拟接口耗时，线程完全阻塞、accept()（服务器侧）：等待网络响应时阻塞



*/
int main(int argc, char* argv[]) {
    // 并发数：默认100，可通过命令行传参（如 ./stress_client 1000）
    int concurrent_count = 100;
    if (argc > 1) {
        concurrent_count = atoi(argv[1]);
    }

    printf("===== 开始压力测试 =====\n");
    printf("并发客户端数：%d\n", concurrent_count);
    printf("服务器地址：%s:%d\n", SERVER_IP, SERVER_PORT);

    // 记录测试开始时间
    long long test_start = get_current_us();

    // 1. 创建多线程模拟并发客户端
    pthread_t* threads = (pthread_t*)malloc(sizeof(pthread_t) * concurrent_count);
    if (!threads) {
        fprintf(stderr, "内存分配失败！\n");
        return -1;
    }

    for (int i = 0; i < concurrent_count; ++i) {
        if (pthread_create(&threads[i], NULL, client_request, NULL) != 0) {
            fprintf(stderr, "创建线程 %d 失败！\n", i);
            pthread_mutex_lock(&g_stat_mutex);
            g_fail_count++;
            pthread_mutex_unlock(&g_stat_mutex);
        }
    }

    // 2. 等待所有线程完成
    for (int i = 0; i < concurrent_count; ++i) {
        pthread_join(threads[i], NULL);
    }

    // 3. 计算测试结果
    long long test_end = get_current_us();
    long long total_time_ms = (test_end - test_start) / 1000; // 总耗时（毫秒）
    double avg_delay_ms = g_success_count > 0 ? (double)g_total_delay / g_success_count / 1000 : 0; // 平均延迟（毫秒）
    double qps = g_success_count > 0 ? (double)g_success_count / (total_time_ms / 1000.0) : 0;    // 每秒请求数

    // 4. 输出测试报告
    printf("===== 测试结果 =====\n");
    printf("总耗时：%lld 毫秒\n", total_time_ms);
    printf("成功请求数：%d\n", g_success_count);
    printf("失败请求数：%d\n", g_fail_count);
    
    int total_request = g_success_count + g_fail_count;
    double success_rate = total_request > 0 ? (double)g_success_count / total_request * 100 : 0;
    printf("成功率：%.2f%%\n", success_rate);
    printf("平均响应延迟：%.2f 毫秒\n", avg_delay_ms);
    printf("QPS（每秒请求数）：%.2f\n", qps);

    // 释放资源
    free(threads);
    pthread_mutex_destroy(&g_stat_mutex);
    return 0;
}