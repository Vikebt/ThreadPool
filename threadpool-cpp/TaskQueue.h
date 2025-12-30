#pragma once
#include <queue>
#include <thread>

// 1. 使用类型别名，提高可读性
using callback = void(*)(void* arg);
// 2. Task结构体：代表一个待执行的工作单元
struct Task
{
	// 默认构造函数
	Task()
	{
		function = nullptr;
		arg = nullptr;
	}
	// 有参构造函数
	Task(callback func, void* argument )
	{
		function = func;
		arg = argument;
	}
	// 任务函数指针
	callback function;
	// 任务参数
	void* arg;
};

// 3. TaskQueue类：封装了线程安全的任务队列
class TaskQueue
{
public:
	TaskQueue();
	~TaskQueue();

	// 添加任务（两种重载，方便使用）
	void addTask(Task task);
	void addTask(callback func, void* arg);
	// 取出一个任务
	Task takeTask();
	// 获取当前任务的个数（内联函数，高效）
	inline size_t taskNumber()
	{
		return m_Task.size();
	}

private:
	// 互斥锁，保护内部队列
	pthread_mutex_t m_mutex;    
	// 使用STL队列，省去手动管理内存
	std::queue<Task> m_Task;
};

