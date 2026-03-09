#ifndef THREDA_POOL_H
#define THREDA_POOL_H


typedef struct thread_pool thread_pool;

//创建线程池并初始化
thread_pool *thread_pool_create(int min_num,int max_num,int queue_size);

void *worker(void *arg);
void *manager(void *arg);
void thread_exit(thread_pool* pool);

//给环形等待队列添加任务（生产者）
void thread_pool_add(thread_pool *pool,void (*func)(void*),void* arg);



#endif// _threadpool_H