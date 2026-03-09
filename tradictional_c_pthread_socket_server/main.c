#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/time.h>

// 全局统计变量
int g_success_count = 0;
int g_fail_count = 0;
long long g_total_delay = 0;
pthread_mutex_t g_stat_mutex = PTHREAD_MUTEX_INITIALIZER;

// 配置参数
#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 8888
#define REQUEST_DATA "186016"
#define CONCURRENT_CONN 1000  // 并发连接数（远低于14000）
#define REQ_PER_CONN 100      // 每个连接循环请求次数（1000×100=10万请求）

// 获取当前时间（微秒）
static long long get_current_us() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000000 + tv.tv_usec;
}

// 单个连接循环发送请求（复用连接）
static void* client_loop_request(void* arg) {
    (void)arg;

    // 1. 创建一个连接（复用）
    int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        pthread_mutex_lock(&g_stat_mutex);
        g_fail_count += REQ_PER_CONN; // 该连接的所有请求都失败
        pthread_mutex_unlock(&g_stat_mutex);
        return NULL;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    if (inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr) <= 0 ||
        connect(sock_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        pthread_mutex_lock(&g_stat_mutex);
        g_fail_count += REQ_PER_CONN;
        pthread_mutex_unlock(&g_stat_mutex);
        close(sock_fd);
        return NULL;
    }

    // 2. 循环发送请求（复用这个连接）
    for (int i = 0; i < REQ_PER_CONN; ++i) {
        long long start_time = get_current_us();

        // 发送请求
        if (send(sock_fd, REQUEST_DATA, strlen(REQUEST_DATA), 0) < 0) {
            pthread_mutex_lock(&g_stat_mutex);
            g_fail_count++;
            pthread_mutex_unlock(&g_stat_mutex);
            continue;
        }

        // 接收响应
        char buffer[1024] = {0};
        ssize_t recv_len = recv(sock_fd, buffer, sizeof(buffer)-1, 0);
        if (recv_len < 0) {
            pthread_mutex_lock(&g_stat_mutex);
            g_fail_count++;
            pthread_mutex_unlock(&g_stat_mutex);
            continue;
        }

        // 统计成功
        long long delay = get_current_us() - start_time;
        pthread_mutex_lock(&g_stat_mutex);
        g_success_count++;
        g_total_delay += delay;
        pthread_mutex_unlock(&g_stat_mutex);
    }

    // 3. 关闭连接
    close(sock_fd);
    return NULL;
}

int main() {
    // 总请求数 = 并发连接数 × 每个连接请求数
    int total_request = CONCURRENT_CONN * REQ_PER_CONN;
    printf("===== 复用连接压测 =====\n");
    printf("并发连接数：%d\n", CONCURRENT_CONN);
    printf("每个连接请求数：%d\n", REQ_PER_CONN);
    printf("总请求数：%d（模拟高并发）\n", total_request);

    long long test_start = get_current_us();

    // 创建线程（每个线程对应一个复用的连接）
    pthread_t* threads = malloc(sizeof(pthread_t) * CONCURRENT_CONN);
    for (int i = 0; i < CONCURRENT_CONN; ++i) {
        if (pthread_create(&threads[i], NULL, client_loop_request, NULL) != 0) {
            pthread_mutex_lock(&g_stat_mutex);
            g_fail_count += REQ_PER_CONN;
            pthread_mutex_unlock(&g_stat_mutex);
        }
    }

    // 等待所有线程完成
    for (int i = 0; i < CONCURRENT_CONN; ++i) {
        pthread_join(threads[i], NULL);
    }

    // 统计结果
    long long test_end = get_current_us();
    long long total_time_ms = (test_end - test_start) / 1000;
    double avg_delay_ms = g_success_count > 0 ? (double)g_total_delay / g_success_count / 1000 : 0;
    double qps = g_success_count > 0 ? (double)g_success_count / (total_time_ms / 1000.0) : 0;

    printf("===== 测试结果 =====\n");
    printf("总耗时：%lld 毫秒\n", total_time_ms);
    printf("成功请求数：%d\n", g_success_count);
    printf("失败请求数：%d\n", g_fail_count);
    printf("成功率：%.2f%%\n", (double)g_success_count/total_request*100);
    printf("平均延迟：%.2f 毫秒\n", avg_delay_ms);
    printf("QPS：%.2f\n", qps);

    free(threads);
    pthread_mutex_destroy(&g_stat_mutex);
    return 0;
}