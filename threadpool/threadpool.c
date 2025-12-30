#include "threadpool.h"
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

const int NUMBER = 2;
// 任务结构体
typedef struct Task 
{
	void (*function)(void* arg); // 任务函数指针：指向返回void，参数为void*的函数
	void* arg;                   // 任务通用指针：可以指向任意类型的参数
}Task;

// 线程池结构体
struct ThreadPool
{
	// 任务队列
	Task* taskQueue;          // 指向任务队列数组的指针
	int queueCapacity;       // 任务队列容量
	int queueSize;           // 当前任务队列大小
	int queueFront;          // 任务队列头索引
	int queueRear;           // 任务队列尾索引

	// 线程管理
	pthread_t managerID; // 管理线程
	pthread_t* workerIDs; // 指向工作线程ID数组的指针
	int minNum;          // 最小线程数
	int maxNum;          // 最大线程数
	int busyNum;         // 忙碌线程数
	int liveNum;         // 存活线程数
	int exitNum;         // 需要销毁的线程数
	
	// 同步机制
	pthread_mutex_t mutexPool;        // 锁整个的线程池
	pthread_mutex_t mutexBusy;        // 锁busyNum线程数
	pthread_cond_t notFull;     // 任务队列不满条件变量
	pthread_cond_t notEmpty;    // 任务队列不空条件变量

	int shutdown;                    // 线程池是否关闭
};

ThreadPool* threadPoolCreate(int min, int max, int queueSize)
{
	// 1.分配线程池结构体内存
	// pool是指向ThreadPool结构体的指针
	// malloc返回void*，需要强制转换为ThreadPool*
	ThreadPool* pool = (ThreadPool*)malloc(sizeof(ThreadPool));
	do
	{
		if (pool == NULL)
		{
			printf("malloc threadpool failed\n");
			break;
		}
		// 2.分配工作线程ID数组
		// workerIDs指向动态分配的线程ID数组
		// pthread_t*：指向线程ID数组的指针，数组需要动态分配，所以用指针
		pool->workerIDs = (pthread_t*)malloc(sizeof(pthread_t) * max);
		if (pool->workerIDs == NULL)
		{
			printf("malloc workerIDs failed\n");
			free(pool);
			break;
		}
		memset(pool->workerIDs, 0, sizeof(pthread_t) * max);

		pool->minNum = min;
		pool->maxNum = max;
		pool->busyNum = 0;
		pool->liveNum = min;
		pool->exitNum = 0;
		// 3.初始化互斥锁和条件变量
		if (pthread_mutex_init(&(pool->mutexPool), NULL) != 0 ||
			pthread_mutex_init(&(pool->mutexBusy), NULL) != 0 ||
			pthread_cond_init(&(pool->notEmpty), NULL) != 0 ||
			pthread_cond_init(&(pool->notFull), NULL) != 0)
		{
			printf("init mutex or cond failed\n");
			break;
		}
		// 4.分配任务队列
		// taskQueue指向动态分配的任务队列数组
		// Task*：指向任务数组的指针
		pool->taskQueue = (Task*)malloc(sizeof(Task) * queueSize);
		pool->queueCapacity = queueSize;
		pool->queueSize = 0;
		pool->queueFront = 0;
		pool->queueRear = 0;

		pool->shutdown = 0;

		// 5.创建管理线程
		// &pool->managerID：取线程ID的地址，pthread_create会填充这个ID
		// pool：传递线程池指针给manager函数
		pthread_create(&(pool->managerID), NULL, manager, pool);

		// 6.创建工作线程
		// &pool->workerID[i]：取数据元素的地址
		// pool：传递线程池指针给worker函数
		for (int i = 0; i < min; i++)
		{
			pthread_create(&(pool->workerIDs[i]), NULL, worker, pool);
		}
		return pool; // 返回创建的线程池指针
	} while (0);

	// 错误处理：释放所有分配的内存
	if (pool && pool->workerIDs) free(pool->workerIDs);
	if (pool && pool->taskQueue) free(pool->taskQueue);
	if (pool) free(pool);

	return NULL;
}

int threadPoolDestroy(ThreadPool* pool)
{
	if (pool == NULL)
	{
		return -1;
	}
	// 关闭线程池
	pool->shutdown = 1;
	// 阻塞回收管理者线程
	pthread_join(pool->managerID,NULL);
	// 唤醒阻塞的消费者线程
	for (int i = 0; i < pool->liveNum; i++)
	{
		pthread_cond_signal(&(pool->notEmpty));
	}
	// 释放堆内存
	if (pool->taskQueue)
	{
		free(pool->taskQueue);
		pool->taskQueue = NULL;
	}
	if (pool->workerIDs)
	{
		free(pool->workerIDs);
		pool->workerIDs = NULL;
	}

	pthread_mutex_destroy(&(pool->mutexPool));
	pthread_mutex_destroy(&(pool->mutexBusy));
	pthread_cond_destroy(&(pool->notEmpty));
	pthread_cond_destroy(&(pool->notFull));

	free(pool);
	pool = NULL;
	return 0;
}

void threadPoolAdd(ThreadPool* pool, void(*func)(void*), void* arg)
{
	pthread_mutex_lock(&(pool->mutexPool));
	// 判断当前任务队列是否已满
	while (pool->queueSize == pool->queueCapacity && !pool->shutdown)
	{
		// 阻塞生产者线程
		pthread_cond_wait(&(pool->notFull), &(pool->mutexPool));
	}
	// 判断线程池是否被关闭
	if (pool->shutdown)
	{
		pthread_mutex_unlock(&(pool->mutexPool));
		return;
	}
	// 添加任务到队列
	pool->taskQueue[pool->queueRear].function = func; // 存储函数指针
	pool->taskQueue[pool->queueRear].arg = arg; // 存储参数指针
	pool->queueRear = (pool->queueRear + 1) % pool->queueCapacity;
	pool->queueSize++;
	// 解锁
	pthread_cond_signal(&(pool->notEmpty));
	pthread_mutex_unlock(&(pool->mutexPool));
}

int threadPoolBusyNum(ThreadPool* pool)
{
	pthread_mutex_lock(&(pool->mutexBusy));
	int busyNum = pool->busyNum;
	pthread_mutex_unlock(&(pool->mutexBusy));
	return busyNum;
}

int threadPoolAliveNum(ThreadPool* pool)
{
	pthread_mutex_lock(&(pool->mutexPool));
	int aliveNum = pool->liveNum;
	pthread_mutex_unlock(&(pool->mutexPool));
	return aliveNum;
}

void* worker(void* arg)
{
	// arg是void*，需要转换为ThreadPool*才能访问成员
	// 这里arg实际上是创建线程时传递的pool指针
	ThreadPool* pool = (ThreadPool*)arg;
	while (1)
	{
		pthread_mutex_lock(&(pool->mutexPool));
		// 判断当前任务队列是否为空
		while (pool->queueSize == 0 && !pool->shutdown)
		{
			// 阻塞工作线程
			pthread_cond_wait(&(pool->notEmpty),&(pool->mutexPool));

			// 判断是不是要销毁线程
			if (pool->exitNum > 0)
			{
				pool->exitNum--;
				if (pool->liveNum > pool->minNum)
				{
					pool->liveNum--;
					pthread_mutex_unlock(&(pool->mutexPool));
					threadExit(pool);
				}
			}
		}
		// 判断线程池是否被关闭
		if (pool->shutdown)
		{
			pthread_mutex_unlock(&(pool->mutexPool));
			threadExit(pool);
		}

		// 从任务队列中取出一个任务
		Task task;
		task.function = pool->taskQueue[pool->queueFront].function; // 复制函数指针
		task.arg = pool->taskQueue[pool->queueFront].arg; // 复制参数指针
		// 移动头结点
		pool->queueFront = (pool->queueFront + 1) % pool->queueCapacity;
		pool->queueSize--;
		// 解锁
		pthread_cond_signal(&(pool->notFull));
		pthread_mutex_unlock(&pool->mutexPool);

		printf("thread %ld start working...\n", pthread_self());
		pthread_mutex_lock(&(pool->mutexBusy));
		pool->busyNum++;
		pthread_mutex_unlock(&(pool->mutexBusy));
		// 执行任务
		task.function(task.arg);
		free(task.arg); // 释放参数内存
		task.arg = NULL;

		printf("thread %ld end working...\n", pthread_self());
		pthread_mutex_lock(&(pool->mutexBusy));
		pool->busyNum--;
		pthread_mutex_unlock(&(pool->mutexBusy));

	}
	return NULL;
}

void* manager(void* arg)
{
	ThreadPool* pool = (ThreadPool*)arg;
	while (!pool->shutdown)
	{
		// 每隔3s检测一次
		sleep(3);

		// 取出线程池中任务的数量和当前线程的数量
		pthread_mutex_lock(&(pool->mutexPool));
		int queueSize = pool->queueSize;
		int liveNum = pool->liveNum;
		pthread_mutex_unlock(&(pool->mutexPool));

		// 取出忙的线程数量
		pthread_mutex_lock(&(pool->mutexPool));
		int busyNum = pool->busyNum;
		pthread_mutex_unlock(&(pool->mutexPool));

		// 添加线程（可自定义）
		// 任务的个数>存活的线程数 && 存活的线程数<最大线程数
		if (queueSize > liveNum && liveNum < pool->maxNum)
		{
			pthread_mutex_lock(&(pool->mutexPool));
			int counter = 0;
			for (int i = 0; i < pool->maxNum && counter < NUMBER && pool->liveNum < pool->maxNum; i++)
			{
				if (pool->workerIDs[i] == 0)
				{
					pthread_create(&(pool->workerIDs[i]), NULL, worker, pool);
					counter++;
					pool->liveNum++;
				}
			}
			pthread_mutex_unlock(&(pool->mutexPool));
		}

		// 销毁线程（可自定义）
		// 忙的线程*2 < 存活的线程数 && 存活的线程数>最小线程数
		if (busyNum * 2 < liveNum && liveNum > pool->minNum)
		{
			pthread_mutex_lock(&(pool->mutexPool));
			pool->exitNum = NUMBER;
			pthread_mutex_unlock(&(pool->mutexPool));
			// 让工作的线程自杀
			for (int i = 0; i < NUMBER; i++)
			{
				pthread_cond_signal(&(pool->notEmpty));
			}
		}
	}
	return NULL;
}

void threadExit(ThreadPool* pool)
{
	pthread_t tid = pthread_self();
	for (int i = 0; i < pool->maxNum; ++i)
	{
		if (pool->workerIDs[i] == tid)
		{
			pool->workerIDs[i] = 0;
			printf("threadExit() called, %ld exiting...\n", tid);
			break;
		}
	}
	pthread_exit(NULL);
}