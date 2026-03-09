
#include<stdio.h>
#include <stdlib.h>
#include<pthread.h>
#include <string.h>
#include<unistd.h>

#include"threadpool.h"

#define ADD_DEL_NUM 2 //增加或删除线程数量

typedef struct task{
    void (*function)(void *arg);//指向一个以void *arg为参数，返回值为void类型的函数
    void *arg;
}task;

//线程池结构体
struct thread_pool{
    //任务队列
    task *task_queue;//环形等待队列:处理待处理的业务
    int queue_capacity;//环形等待队列的容量，(是用户传递的大小）
    int queue_size;//环形等待队列剩余待处理任务数量（实际就是仓库的剩余物品数）
    int queue_front;//队头：取数据
    int queue_rear;//队尾：放数据

    pthread_t manager_id;//管理者线程id
    pthread_t *worker_ids;//指向 工作的线程id的数组

    //线程池结构
    int min_num;//池线程最小容量(应该是即使没有忙线程也会保持一个最小数的线程在运行)
    int max_num;//池线程最大容量
    int busy_num;//忙碌线程个数,也即正在处理业务的线程
    int live_num;//存活的线程个数，有一部分线程可能空闲没处理业务
    int exit_num;//要销毁的线程个数

    pthread_mutex_t mutex_pool;//锁住整个线程
    pthread_mutex_t mutex_busy;//锁住busy_num,提高效率？？？
    pthread_cond_t not_full;//
    pthread_cond_t not_empty;//   

    int shutdown;//是不是要销毁线程，销毁是1，不销毁为0
};


thread_pool *thread_pool_create(int min_num,int max_num,int queue_capacity){
    int i;
    thread_pool *pool;

    do{
        pool=(thread_pool *)malloc(sizeof(thread_pool));
        if(NULL==pool){
            printf("malloc pool error");
            break;
        }

        //
        pool->worker_ids=(pthread_t*)malloc(sizeof(pthread_t)*max_num);
        if(NULL==pool->worker_ids){
            printf("malloc pool->worker_ids error");
            break;
        }
        memset(pool->worker_ids,0,sizeof(pthread_t)*max_num);

        pool->min_num=min_num;
        pool->max_num=max_num;
        pool->busy_num=0;
        pool->live_num=min_num;
        pool->exit_num=0;

        if(pthread_mutex_init(&pool->mutex_pool,NULL)!=0 ||
        pthread_mutex_init(&pool->mutex_busy,NULL)!=0 ||
        pthread_cond_init(&pool->not_empty,NULL)!=0 ||
        pthread_cond_init(&pool->not_full,NULL)!=0  
        ){
            printf("mmutex_init or pthread_cond_init error");
            break;
        }

        //任务队列
        pool->task_queue=(task*)malloc(sizeof(task)*queue_capacity);//数组充当环形队列
        pool->queue_capacity=queue_capacity;//我认为pool->queue_capacity=queue_size作者是错的，他似乎把变量意义搞错了，但也能运行起来
        pool->queue_size=0;
        pool->queue_front=0;
        pool->queue_rear=0;

        //未关闭
        pool->shutdown=0;

        //创建线程
        pthread_create(&pool->manager_id,NULL,manager,pool);//创建一个管理者
        for(i=0;i<min_num;++i){
            pthread_create(&pool->worker_ids[i],NULL,worker,pool);//创建最少量即min_num个工作线程
        }    

        return pool;
    }while (0);//替代goto

    //释放资源
    if(pool&&pool->worker_ids) free(pool->worker_ids);
    if(pool&&pool->task_queue) free(pool->task_queue);
    if(pool) free(pool);//我认为这一句应该放在上面两句之后！？

    return NULL;
}



// 记忆：
// 特征是双层夹子夹住操作。
// while()中判断MAX或者0：
//         while ($1 == MAX){ 
//             pthread_cond_wait(&$2, &mutex);
// }
// //一顿操作
// pthread_cond_signal(&$3);
//
// $1填MAX，$2则填not_fill（正好相反）
// $1填0，$2则填not_empty（正好相反）
// $3也永远和$2相反


//线程池中的空闲线程竞争环形等待队列的任务(消费者)
void *worker(void *arg){
    thread_pool* pool=(thread_pool*)arg;//直接把线程池传进来么！？

    while (1){
        pthread_mutex_lock(&pool->mutex_pool);
/*******************************第一层：临界区********************************/
        while (pool->queue_size==0&&!pool->shutdown){//不用if用while解决假唤醒问题
            //阻塞工作线程
            pthread_cond_wait(&pool->not_empty,&pool->mutex_pool);//发现条件不满足，后悔了，会又解开mutex_pool的锁//直到信号通知不空not_empty不再阻塞

            //管理者通知销毁空闲线程：由管理者线程发出通知 pool->exit_num=ADD_DEL_NUM ，使得本空闲线程自杀（应该不是虚假唤醒，因为只唤醒了一个，我感觉把这段放在两层夹子中似乎也可以？？？）
            if(pool->exit_num>0){
                pool->exit_num--;//似乎还有更多的问题？？？

                //只有比最小允许线程多才允许销毁
                if(pool->live_num>pool->min_num){
                    pool->live_num--;//存活线程-1，这里销毁的肯定是空闲线程
                    pthread_mutex_unlock(&pool->mutex_pool);//临死前释放线程池锁的所有权
                    thread_exit(pool);
                }
            }
        }
/***************** 第二层：条件变量夹子********************************/
        //判断线程池是否被关闭了，如果没有这句话会怎么样？？？
        if(pool->shutdown){
            pthread_mutex_unlock(&pool->mutex_pool);
            thread_exit(pool);
        }

        //从环形队列取出一个任务放进任务结构体
        task task;
        //从环形队列头部取出一个数据
        task.function=pool->task_queue[pool->queue_front].function;
        task.arg=pool->task_queue[pool->queue_front].arg;//注意这个 任务队列.arg 和传进来的pool那个arg不是一个东西
        //移动环形队列的头指针
        pool->queue_front=(pool->queue_front+1)%pool->queue_capacity;//经典取余操作
        pool->queue_size--;//仓库物品-1
/********************************************************************/
        pthread_cond_signal(&pool->not_full);//我目前对于条件变量实现的生产消费者模型的理解是双层夹子，最里面放临界资源
/************************************************************************/
        pthread_mutex_unlock(&pool->mutex_pool);

        //访问忙线程资源：要加锁
        pthread_mutex_lock(&pool->mutex_busy);
        pool->busy_num++;
        pthread_mutex_unlock(&pool->mutex_busy);
        printf("当前工作线程%ld开始工作...\n",pthread_self());

        //在这里执行本线程！！！
        task.function(task.arg);//task.arg就是当前任务的参数//task.function()函数指针指向了一个函数
        //free(task.arg);//task.arg是用户传入函数的参数地址（怎么解析这个参数是用户自己的事情），我认为不应该由我释放，不然用户传一个栈空间数据我来free会报错
        task.arg=NULL;

        //访问忙线程资源：要加锁
        pthread_mutex_lock(&pool->mutex_busy);
        pool->busy_num--;
        pthread_mutex_unlock(&pool->mutex_busy);
        printf("当前工作线程%ld结束工作...\n",pthread_self());

    }
    return NULL;
}

//（管理者）
void *manager(void *arg){
    int queue_size;
    int live_num;
    int busy_num;
    int count;
    int i;

    thread_pool *pool=(thread_pool*)arg;
    while (!pool->shutdown){//不销毁就每隔3s进行一次管理操作
        //每隔3s检测一次
        sleep(3);
    
        //取出线程池中任务的存活的线程个数、当前任务个数
        pthread_mutex_lock(&pool->mutex_pool);
        queue_size=pool->queue_size;
        live_num=pool->live_num;
        pthread_mutex_unlock(&pool->mutex_pool);

        //取出忙碌线程个数:也可以用上面pool->mutex_pool来锁住，但由于经常访问所以单独锁住粒度更小
        pthread_mutex_lock(&pool->mutex_busy);
        busy_num=pool->busy_num;
        pthread_mutex_unlock(&pool->mutex_busy);   

        //添加线程
        //方式：任务个数>存活的线程个数&&存活的线程数量<最大线程数(硬性条件)
        if(queue_size>live_num&&live_num<pool->max_num){//发现环形等待队列中的任务比空闲线程多（显然应该添加任务了），存活线程比线程池数组小（硬性条件）

            //锁住线程池
            pthread_mutex_lock(&pool->mutex_pool);
            count=0;
            for(i=0;i<pool->max_num&&
                    count<ADD_DEL_NUM&&
                    pool->live_num<pool->max_num;++i){//必须增加两个线程、遍历寻找worker_ids[i]==0的位置、存活线程池还不能超过最大线程数
                if(pool->worker_ids[i]==0){
                    pthread_create(&pool->worker_ids[i],NULL,worker,pool);
                    pool->live_num++;
                    count++;
                }

            }
            pthread_mutex_unlock(&pool->mutex_pool);
        }

        //销毁线程
        //忙的线程*2<存活的线程&&存活的线程>最小线程数
        if(busy_num*2<live_num&&live_num>pool->min_num){//空闲线程比正在工作的线程多一倍（太多了）&&存活线程大于最小线程（硬性条件）

            //还是用线程池的锁访问条件变量
            pthread_mutex_lock(&pool->mutex_pool);
            pool->exit_num=ADD_DEL_NUM;
            pthread_mutex_unlock(&pool->mutex_pool);

            //让工作的线程自杀，因为无法主动杀死它？？？
            for(i=0;i<ADD_DEL_NUM;++i){
                pthread_cond_signal(&pool->not_empty);//假装唤醒//和pthread_cond_broadcast()唤醒多个效果一样，因为唤醒多个都要去抢那把锁
            }
        }
    }
    return NULL;
}

//当前线程退出,并将pool->worker_ids][i]置为0以供后面使用//这玩意明显可以用哈希表代替
void thread_exit(thread_pool* pool){
    int i;

    pthread_t tid=pthread_self();
    for ( i = 0; i < pool->max_num; i++){
        if(pool->worker_ids[i]==tid){
            pool->worker_ids[i]=0;//置为0，以供后面使用
            printf("thread_exit()被调用,%ld 线程退出...\n",tid);
            break;
        }
    }
    pthread_exit(NULL);
}

//给环形等待队列添加任务（生产者）
void thread_pool_add(thread_pool *pool,void (*func)(void*),void* arg){

    pthread_mutex_lock(&pool->mutex_pool);
/**********************************第一层：临界区********************************/
    while (pool->queue_size==pool->queue_capacity && !pool->shutdown){
        //阻塞生产者线程
        pthread_cond_wait(&pool->not_full,&pool->mutex_pool);
    }
/*************************第二层：条件变量夹子*********************/
    //要销毁线程池
    if(pool->shutdown){
        pthread_mutex_unlock(&pool->mutex_pool);
        return;
    }
    
    //把任务先添加到环形等待队列
    pool->task_queue[pool->queue_rear].function=func;
    pool->task_queue[pool->queue_rear].arg=arg;
    pool->queue_rear=(pool->queue_rear+1)%pool->queue_capacity;
    pool->queue_size++;

/****************************************************************/
    pthread_cond_signal(&pool->not_empty);
/*************************************************************************/
    pthread_mutex_unlock(&pool->mutex_pool);
}

//线程销毁
int thread_pool_destroy(thread_pool *pool){
    int i;

    if(pool==NULL){
        return -1;
    }

    //关闭线程池
    pool->shutdown=1;//通知管理者线程/消费者线程关闭

    //阻塞回收：管理者线程  
    pthread_join(pool->manager_id,NULL);

    //唤醒阻塞的消费者线程通知其结束：会自动销毁？？？
    for(i=0;i<pool->live_num;++i){
        pthread_cond_signal(&pool->not_empty);//唤醒多个和一个都一样
    }

    //释放条件变量和锁
    pthread_mutex_destroy(&pool->mutex_pool);
    pthread_mutex_destroy(&pool->mutex_busy);
    pthread_cond_destroy(&pool->not_empty);
    pthread_cond_destroy(&pool->not_full);

    //释放堆内存
    if(pool->task_queue){
        free(pool->task_queue);
    }
    if(pool->worker_ids){
        free(pool->worker_ids);
    }
    free(pool);
    pool=NULL;

    return 0;
}


//获取线程池中正在工作的线程个数
int thread_pool_busy_num(thread_pool *pool){
    int busy_num;

    //读取的时候似乎不用加锁？？？
    pthread_mutex_lock(&pool->mutex_busy);
    busy_num=pool->busy_num;
    pthread_mutex_unlock(&pool->mutex_busy);

    return busy_num;
}


//获取线程池中活着的线程个数
int thread_pool_live_num(thread_pool *pool){
    int live_num;

    pthread_mutex_lock(&pool->mutex_pool);
    live_num=pool->live_num;
    pthread_mutex_unlock(&pool->mutex_pool);
    
    return live_num;
}

/**********************测试***********************/
void task_func(void *arg){
    int *num=(int *)arg;
    printf("当前线程%ld正在工作,number=%d\n",pthread_self(),*num);
    sleep(1);
}


// int main(){
//     thread_pool *pool;
//     int i;
//     int *num;

//     //创建线程池
//     pool=thread_pool_create(3,10,100);

//     for(i=0;i<100;++i){
//         num=(int*)malloc(sizeof(int));
//         *num=i+100;
//         thread_pool_add(pool,task_func,num);
//     }

//     sleep(10);//主线程等待，等待子进程执行结束
//     thread_pool_destroy(pool);

//     free(num);//我认为堆空间的数据应该由用户自己销毁
//     return 0;
// }







