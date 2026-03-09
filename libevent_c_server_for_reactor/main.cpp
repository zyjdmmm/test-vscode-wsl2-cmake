#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <event2/event.h>
#include <event2/listener.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/thread.h>
#include <errno.h>

#define CPU_CORES 8  // 适配你的8核CPU
#define SERVER_PORT 8888
#define BUFFER_SIZE 1024
#define DELAY_MS 10  // 模拟接口耗时

// 从线程结构体：绑定事件循环和CPU核心
typedef struct {
    struct event_base* base;
    int cpu_id;
} worker_thread_t;

worker_thread_t workers[CPU_CORES];  // 8个从线程
int next_worker = 0;  // 轮询分发索引
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

// 自定义结构体：存储超时回调数据
typedef struct {
    struct bufferevent* bev;
    char* response;
} timeout_data_t;

// 超时事件回调：耗时结束后发送响应
static void timeout_cb(evutil_socket_t fd, short events, void* arg) {
    timeout_data_t* td = (timeout_data_t*)arg;
    if (!td || !td->bev || !td->response) {
        free(td);
        return;
    }

    // 异步发送响应
    bufferevent_write(td->bev, td->response, strlen(td->response));
    
    // 释放资源
    free(td->response);
    bufferevent_free(td->bev);
    free(td);
}

// 模拟网易云接口处理（异步非阻塞）
static void simulate_netease_api(const char* song_id, struct bufferevent* bev) {
    struct event_base* base = bufferevent_get_base(bev);
    struct timeval tv = {0, DELAY_MS * 1000};  // 10ms超时

    // 封装响应数据
    char response[BUFFER_SIZE] = {0};
    snprintf(response, sizeof(response), "查询歌曲ID：%s | 响应：周杰伦-七里香\n", song_id);

    // 分配自定义数据结构
    timeout_data_t* td = (timeout_data_t*)malloc(sizeof(timeout_data_t));
    td->bev = bev;
    td->response = strdup(response);

    // 创建超时事件
    struct event* timeout_ev = event_new(base, -1, 0, timeout_cb, td);
    event_add(timeout_ev, &tv);
    event_free(timeout_ev);
}

// 客户端读事件回调（从线程处理）
static void on_read(struct bufferevent* bev, void* ctx) {
    struct evbuffer* input = bufferevent_get_input(bev);
    size_t len = evbuffer_get_length(input);
    if (len == 0) return;

    // 读取客户端数据
    char* buf = (char*)malloc(len + 1);
    evbuffer_remove(input, buf, len);
    buf[len] = '\0';

    // 打印当前处理的从线程ID
    worker_thread_t* worker = (worker_thread_t*)ctx;
    printf("[从线程CPU%d] 收到请求：%s\n", worker->cpu_id, buf);

    // 异步处理接口请求
    simulate_netease_api(buf, bev);
    free(buf);
}

// 客户端事件回调（断开/错误）
static void on_event(struct bufferevent* bev, short events, void* ctx) {
    if (events & (BEV_EVENT_EOF | BEV_EVENT_ERROR | BEV_EVENT_TIMEOUT)) {
        bufferevent_free(bev);
        worker_thread_t* worker = (worker_thread_t*)ctx;
        printf("[从线程CPU%d] 客户端断开连接\n", worker->cpu_id);
    }
}

// 从线程入口：启动事件循环（绑定CPU核心）
static void* worker_thread(void* arg) {
    worker_thread_t* worker = (worker_thread_t*)arg;
    
    // 绑定线程到指定CPU核心
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(worker->cpu_id, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);

    // 启动从线程的事件循环
    printf("从线程%d（绑定CPU%d）启动，事件循环：%p\n", 
           worker->cpu_id, worker->cpu_id, worker->base);
    event_base_dispatch(worker->base);
    return NULL;
}

// 连接分发回调：主线程接收连接后分发给从线程
static void on_accept(struct evconnlistener* listener, evutil_socket_t fd,
                      struct sockaddr* addr, int len, void* ptr) {
    // 轮询选择下一个从线程（负载均衡）
    pthread_mutex_lock(&mutex);
    int idx = next_worker % CPU_CORES;
    next_worker++;
    pthread_mutex_unlock(&mutex);
    worker_thread_t* worker = &workers[idx];

    // 设置Socket为非阻塞模式
    evutil_make_socket_nonblocking(fd);
    evutil_make_listen_socket_reuseable(fd);

    // 创建bufferevent（适配旧版libevent，去掉BEV_OPT_CLOSE_ON_FREE）
    struct bufferevent* bev = bufferevent_socket_new(worker->base, fd, 0);
    if (!bev) {
        close(fd);
        return;
    }

    // 设置回调函数（传递从线程上下文）
    bufferevent_setcb(bev, on_read, NULL, on_event, worker);
    // 启用读事件
    bufferevent_enable(bev, EV_READ);
}

int main() {
    // 初始化libevent线程支持（必须）
    evthread_use_pthreads();

    // 1. 创建8个从线程（每个绑定1个CPU核心）
    for (int i = 0; i < CPU_CORES; i++) {
        workers[i].cpu_id = i;
        workers[i].base = event_base_new();  // 每个从线程独立事件循环
        pthread_t tid;
        pthread_create(&tid, NULL, worker_thread, &workers[i]);
        pthread_detach(tid);
    }

    // 2. 主线程：创建监听事件（仅负责接收连接）
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(SERVER_PORT);

    // 创建监听器（主线程核心逻辑）
    struct evconnlistener* listener = evconnlistener_new_bind(
        NULL, on_accept, NULL,
        LEV_OPT_REUSEABLE | LEV_OPT_CLOSE_ON_FREE,
        1024, (struct sockaddr*)&server_addr, sizeof(server_addr)
    );

    if (!listener) {
        perror("创建监听器失败");
        return -1;
    }

    printf("===== 多核libevent服务器启动 =====\n");
    printf("CPU核心数：%d | 主线程接收连接 | 从线程处理业务\n", CPU_CORES);
    printf("服务器监听端口：%d\n", SERVER_PORT);

    // 主线程阻塞（仅等待连接事件）
    event_base_dispatch(event_base_new());

    // 资源释放
    evconnlistener_free(listener);
    pthread_mutex_destroy(&mutex);
    return 0;
}