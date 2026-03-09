#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <event2/event.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <errno.h>

#define SERVER_PORT 8888
#define BUFFER_SIZE 1024
#define DELAY_MS 10  // 模拟网易云接口处理耗时

// 自定义结构体：存储响应数据（替代user_data）
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

    // 异步发送响应给客户端
    bufferevent_write(td->bev, td->response, strlen(td->response));
    
    // 释放资源
    free(td->response);
    bufferevent_free(td->bev);
    free(td);
}

// 模拟网易云接口处理（异步非阻塞版）
static void simulate_netease_api(const char* song_id, struct bufferevent* bev) {
    // 用libevent的超时事件模拟接口耗时（非阻塞）
    struct event_base* base = bufferevent_get_base(bev);
    struct timeval tv = {0, DELAY_MS * 1000};  // 10ms超时

    // 封装响应数据
    char response[BUFFER_SIZE] = {0};
    snprintf(response, sizeof(response), "查询歌曲ID：%s | 响应：周杰伦-七里香\n", song_id);

    // 分配自定义数据结构（替代user_data存储响应）
    timeout_data_t* td = (timeout_data_t*)malloc(sizeof(timeout_data_t));
    td->bev = bev;
    td->response = strdup(response);

    // 创建超时事件（异步处理耗时任务）
    struct event* timeout_ev = event_new(base, -1, 0, timeout_cb, td);
    event_add(timeout_ev, &tv);
    event_free(timeout_ev);  // libevent自动管理生命周期
}

// 处理客户端读事件（客户端发数据过来）
static void on_read(struct bufferevent* bev, void* ctx) {
    struct evbuffer* input = bufferevent_get_input(bev);
    size_t len = evbuffer_get_length(input);
    if (len == 0) return;

    // 读取客户端发送的歌曲ID（非阻塞读取）
    char* buf = (char*)malloc(len + 1);
    evbuffer_remove(input, buf, len);
    buf[len] = '\0';

    printf("[libevent服务器] 收到请求：%s | 当前事件循环线程数：1\n", buf);

    // 异步处理网易云接口请求
    simulate_netease_api(buf, bev);
    free(buf);
}

// 处理客户端事件（断开/错误）
static void on_event(struct bufferevent* bev, short events, void* ctx) {
    if (events & (BEV_EVENT_EOF | BEV_EVENT_ERROR | BEV_EVENT_TIMEOUT)) {
        // 释放资源（无线程资源需要清理！）
        bufferevent_free(bev);
        printf("[libevent服务器] 客户端断开连接 | 事件循环线程数：1\n");
    }
}

// 处理新客户端连接（监听事件回调）
static void on_accept(evutil_socket_t listener_fd, short events, void* ctx) {
    struct event_base* base = (struct event_base*)ctx;
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);

    // 接受客户端连接（非阻塞accept）
    int client_fd = accept(listener_fd, (struct sockaddr*)&client_addr, &client_len);
    if (client_fd < 0) {
        perror("accept失败");
        return;
    }

    // 设置Socket为非阻塞模式（libevent要求）
    evutil_make_socket_nonblocking(client_fd);
    evutil_make_listen_socket_reuseable(client_fd);  // 端口复用

    printf("[libevent服务器] 新客户端连接：%s:%d\n", 
           inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

    // 创建bufferevent（封装Socket的异步读/写/事件）
    struct bufferevent* bev = bufferevent_socket_new(base, client_fd, BEV_OPT_CLOSE_ON_FREE);
    if (!bev) {
        close(client_fd);
        return;
    }

    // 设置回调函数（读事件/错误事件）
    bufferevent_setcb(bev, on_read, NULL, on_event, base);
    // 启用读事件（写事件按需启用）
    bufferevent_enable(bev, EV_READ);
}

int main() {
    // 1. 创建TCP Socket
    int listener_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listener_fd < 0) {
        perror("socket创建失败");
        exit(EXIT_FAILURE);
    }

    // 2. 设置Socket选项（端口复用）
    int opt = 1;
    setsockopt(listener_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt));
    evutil_make_socket_nonblocking(listener_fd);  // 监听Socket设为非阻塞

    // 3. 绑定IP和端口
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(SERVER_PORT);
    if (bind(listener_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind失败");
        close(listener_fd);
        exit(EXIT_FAILURE);
    }

    // 4. 监听端口
    if (listen(listener_fd, 1024) < 0) {
        perror("listen失败");
        close(listener_fd);
        exit(EXIT_FAILURE);
    }

    // 5. 创建libevent的事件循环（Reactor核心）
    struct event_base* base = event_base_new();
    if (!base) {
        perror("event_base创建失败");
        close(listener_fd);
        exit(EXIT_FAILURE);
    }

    // 6. 创建监听事件（有新连接时触发on_accept）
    struct event* listen_ev = event_new(base, listener_fd, EV_READ | EV_PERSIST, on_accept, base);
    event_add(listen_ev, NULL);  // 将事件加入事件循环

    // 7. 启动事件循环（单线程处理所有并发连接！）
    printf("[libevent服务器] 启动成功：0.0.0.0:%d | 事件循环线程数：1\n", SERVER_PORT);
    event_base_dispatch(base);

    // 8. 资源释放
    event_free(listen_ev);
    event_base_free(base);
    close(listener_fd);
    return 0;
}