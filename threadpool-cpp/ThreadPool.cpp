#include "ThreadPool.h"
#include "iostream"
using namespace std;

ThreadPool::ThreadPool(int min, int max)
{
	do  // 使用 do-while(0) 技巧实现“单入口错误处理”
	{
		// 1. 创建任务队列对象（聚合关系，线程池拥有它）
		m_taskQ = new TaskQueue;
		if (m_taskQ == nullptr)
		{
			cout << "malloc thread_t[] 失败...." << endl;;
			break;
		}
		// 2. 为工作线程ID数组分配堆内存（大小为max）
		m_workerIDs = new pthread_t[max];
		if (m_workerIDs == nullptr)
		{
			cout << "malloc workerIDs failed..." << endl;
			break;
		}
		// 初始化为0，0表示数组位置空闲
		memset(m_workerIDs, 0, sizeof(pthread_t) * max);
		// 3. 初始化线程池状态参数
		m_minNum = min;	// 核心线程数
		m_maxNum = max;	// 最大线程数
		m_busyNum = 0;	// 初始忙碌线程为0
		m_liveNum = min; // 初始存活线程数 = 核心线程数 
		m_exitNum = 0;	// 无需退出任何线程
		// 4. 初始化同步原语（互斥锁、条件变量）
		if (pthread_mutex_init(&(m_lock), NULL) != 0 ||
			pthread_cond_init(&(m_notEmpty), NULL) != 0 )
		{
			cout << "init mutex or cond failed\n";
			break;	// 初始化失败，跳转到错误处理
		}
		m_shutdown = false;	// 线程池处于运行状态

		// 5. 创建管理者线程 (1个) - “监工”
		// 关键：将当前对象指针(this)作为参数传递，使静态函数能访问对象成员
		pthread_create(&(m_managerID), NULL, manager, this); 

		// 6. 创建初始工作线程 (min个) - “工人”
		for (int i = 0; i < min; i++)
		{
			pthread_create(&(m_workerIDs[i]), NULL, worker, this); // 同样传入this指针！
		}
		// 7. 所有步骤成功，直接返回，跳过下面的错误处理代码
		return; 
	} while (0);

	// 8. 错误处理区块（如果上面的任何一步break，就会执行到这里）
	// 释放已分配的资源，避免内存泄漏
	if (m_workerIDs) delete[] m_workerIDs;	// 释放数组（使用 delete[]）
	if (m_taskQ) delete m_taskQ ;	// 释放对象（使用 delete）
	// 注意：构造函数失败，对象不会被创建，所以没有析构函数来清理这些
}

ThreadPool::~ThreadPool()
{
	// 1. 设置关闭标志，通知所有线程准备退出
	m_shutdown = true;
	// 2. 等待管理者线程结束（阻塞等待，确保管理者先退出）
	pthread_join(m_managerID, NULL);
	// 3. 唤醒所有可能阻塞在条件变量上的工作线程
	//    它们被唤醒后，会检查 m_shutdown 标志，然后自行退出
	for (int i = 0; i < m_liveNum; i++)
	{
		pthread_cond_signal(&(m_notEmpty));
	}
	// 注意：这里无法保证100%唤醒所有线程，更稳健的做法需要更复杂的同步。
	// 4. 释放动态分配的内存资源（注意使用delete和delete[]）
	if (m_taskQ) delete m_taskQ; // 删除任务队列对象
	if (m_workerIDs) delete[] m_workerIDs; // 删除线程ID数组
	// 5. 销毁同步原语（锁和条件变量）
	pthread_mutex_destroy(&m_lock);
	pthread_cond_destroy(&m_notEmpty);
	// 此时，工作线程可能还在退出过程中，但主要资源已释放。
}

// 添加任务（生产者调用）
void ThreadPool::addTask(Task task)
{
	pthread_mutex_lock(&(m_lock));
	// 判断线程池是否被关闭,如果池已关闭,不再接受新任务
	if (m_shutdown)
	{
		pthread_mutex_unlock(&(m_lock));
		return;
	}
	// 添加任务到队列,任务队列内部有自己的锁,保证线程安全
	m_taskQ->addTask(task);
	// 解锁
	pthread_mutex_unlock(&(m_lock));
	pthread_cond_signal(&(m_notEmpty));
}

// 查询函数
int ThreadPool::getBusyNumber()
{
	pthread_mutex_lock(&(m_lock));
	int busyNum = m_busyNum;
	pthread_mutex_unlock(&(m_lock));
	return busyNum;
}
int ThreadPool::getLiveNumber()
{
	pthread_mutex_lock(&(m_lock));
	int liveNum = m_liveNum;
	pthread_mutex_unlock(&(m_lock));
	return liveNum;
}

void* ThreadPool::worker(void* arg)
{
	// 关键：转换回对象指针
	ThreadPool* pool = static_cast<ThreadPool*>(arg);
	// 无限循环，直到被告知退出
	while (1)
	{
		// 访问任务队列(共享资源)加锁
		pthread_mutex_lock(&(pool->m_lock));
		// 条件等待：如果任务队列为空 且 线程池没关闭，就阻塞等待
		// 使用 while 而非 if，是为了防止“虚假唤醒”(spurious wakeup)
		while (pool->m_taskQ->taskNumber() == 0 && !pool->m_shutdown)
		{
			// 此函数会：1. 解锁m_lock; 2. 阻塞线程; 3. 被唤醒后，重新加锁m_lock
			pthread_cond_wait(&(pool->m_notEmpty), &(pool->m_lock));

			// 被唤醒后，检查管理者是否让我退出（缩容逻辑）
			if (pool->m_exitNum > 0)
			{
				pool->m_exitNum--;
				if (pool->m_liveNum > pool->m_minNum)	// 确保不少于核心线程数
				{
					pool->m_liveNum--;
					pthread_mutex_unlock(&(pool->m_lock));
					pool->threadExit();	// 调用成员函数执行清理并退出线程
				}
			}
		}
		// 检查：如果线程池已关闭，则退出
		if (pool->m_shutdown)
		{
			pthread_mutex_unlock(&(pool->m_lock));
			pool->threadExit();
		}

		// 从任务队列中取出一个任务
		Task task = pool->m_taskQ->takeTask();	// 从队列取出任务（内部有锁）
		// 工作的线程+1
		pool->m_busyNum++;
		// 线程池解锁
		pthread_mutex_unlock(&pool->m_lock);	// 关键：取到任务后立即释放池锁！
		// >>> 执行任务（在锁外执行，这是并发的关键！） <<<
		cout << "thread " << to_string(pthread_self()) << " start working..." << endl;
		task.function(task.arg);	// 执行用户的任务函数
		delete task.arg; // 假设arg是堆内存，需释放（由任务提交者分配）
		task.arg = nullptr;
		// 任务处理结束
		cout << "thread " << to_string(pthread_self()) << " end working..." << endl;
		// 任务完成，更新状态
		pthread_mutex_lock(&(pool->m_lock));
		pool->m_busyNum--;
		pthread_mutex_unlock(&(pool->m_lock));
	}
	return nullptr;	// 理论上不会执行到这里
}

void* ThreadPool::manager(void* arg)
{
	// 关键：转换回对象指针
	ThreadPool* pool = static_cast<ThreadPool*>(arg);
	// 只要池没关闭，就一直巡逻
	while (!pool->m_shutdown)
	{
		// 每3秒检查一次（可根据负载调整）
		sleep(3);
		// --- 采样阶段：获取当前系统快照 ---
		// 取出线程池中任务的数量和当前线程的数量
		pthread_mutex_lock(&(pool->m_lock));	// 待处理任务数
		int queueSize = pool->m_taskQ->taskNumber();	// 当前总线程数
		int liveNum = pool->m_liveNum;	// 存活线程数
		int busyNum = pool->m_busyNum;	// 忙碌线程数
		pthread_mutex_unlock(&(pool->m_lock));

		// --- 决策阶段1：判断是否需要扩容 ---
		// 策略：有积压任务(queueSize > liveNum) 且 还能创建更多线程(liveNum < max)
		if (queueSize > liveNum && liveNum < pool->m_maxNum)
		{
			pthread_mutex_lock(&(pool->m_lock));
			int counter = 0;
			// 遍历线程ID数组，寻找空位(值为0)创建新线程
			for (int i = 0; i < pool->m_maxNum && counter < NUMBER && pool->m_liveNum < pool->m_maxNum; i++)
			{
				if (pool->m_workerIDs[i] == 0)
				{
					pthread_create(&(pool->m_workerIDs[i]), NULL, worker, pool);
					counter++;
					pool->m_liveNum++;	// 更新存活线程计数
				}
			}
			pthread_mutex_unlock(&(pool->m_lock));
		}

		// --- 决策阶段2：判断是否需要缩容 ---
		// 策略：线程太闲(busyNum*2 < liveNum) 且 能减少一些线程(liveNum > min)
		if (busyNum * 2 < liveNum && liveNum > pool->m_minNum)
		{
			pthread_mutex_lock(&(pool->m_lock));
			pool->m_exitNum = NUMBER;	// 设置退出指令
			pthread_mutex_unlock(&(pool->m_lock));
			// 发送信号，唤醒NUMBER个可能正在等待的线程，让它们检查exitNum并退出
			for (int i = 0; i < NUMBER; i++)
			{
				pthread_cond_signal(&(pool->m_notEmpty));
			}
		}
	}
	return nullptr;
}
// 单个线程退出时的清理
void ThreadPool::threadExit()
{
	pthread_t tid = pthread_self();
	for (int i = 0; i < m_maxNum; ++i)
	{
		if (m_workerIDs[i] == tid)
		{
			m_workerIDs[i] = 0;	// 将自己的ID位置重置为空闲
			cout << "threadExit() function: thread "
				<< to_string(pthread_self()) << " exiting..." << endl;
			break;
		}
	}
	pthread_exit(NULL);	// 终止当前线程
}