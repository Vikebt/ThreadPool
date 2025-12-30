#ifndef _THREADPOOL_H_
#define _THREADPOOL_H_

// 线程池结构体(前向声明，隐藏实现细节)
typedef struct ThreadPool ThreadPool;

// 创建线程池并初始化
// 返回ThreadPool*：因为需要返回动态分配的结构体指针
ThreadPool* threadPoolCreate(int min, int max, int queueSize);

// 销毁线程池
// 参数用ThreadPool*：需要修改线程池状态，且避免结构体拷贝
int threadPoolDestroy(ThreadPool* pool);

// 给线程池添加任务
// ThreadPool* pool:要操作哪个线程池
// void(*func)(void*)：函数指针，用户定义的任务函数
// void* arg：通用指针，传递给任务函数的指针
void threadPoolAdd(ThreadPool* pool, void(*func)(void*), void* arg);

// 获取线程池中工作的线程的个数
int threadPoolBusyNum(ThreadPool* pool);

// 获取线程池中活着的线程的个数
int threadPoolAliveNum(ThreadPool* pool);

///////////////////////////
// 工作的线程(消费者线程)任务函数
void* worker(void* arg);

// 管理者线程任务函数
void* manager(void* arg);

// 单个线程退出
void threadExit(ThreadPool* pool);

#endif // _THREADPOOL_H_