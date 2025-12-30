#pragma once
// 1. 防止头文件被重复包含，编译器支持
#include <queue>
#include <thread>
#include <unistd.h>
#include <string.h>
// 2. 包含任务队列类声明
#include "TaskQueue.h"

class ThreadPool
{
public:
    // 3. 构造函数与析构函数：管理线程池生命周期
    ThreadPool(int min, int max);
    ~ThreadPool();

    // 4. 对外的API（生产者接口）
    void addTask(Task task); // 向池中添加任务（生产者接口）
    int getBusyNumber();     // 获取忙碌线程数
    int getLiveNumber();    // 获取存活线程数
private:
    // 5. 关键的静态成员函数（难点！）
    // 必须为静态函数，才能作为pthread_create的入口点（C风格回调）
    static void* worker(void* arg);   // 所有工作线程的入口函数
    static void* manager(void* arg);  // 管理者线程的入口函数
    // 6. 普通的成员函数
    void threadExit();                // 线程退出时的清理工作

private: 
    // 7. 成员变量（封装线程池状态）
    pthread_mutex_t m_lock;          // 互斥锁：保护线程池整体状态
    pthread_cond_t m_notEmpty;       // 条件变量：任务队列“不空”的信号
    pthread_t* m_workerIDs;          // 动态数组：存储工作线程ID
    pthread_t m_managerID;           // 管理者线程ID
    TaskQueue* m_taskQ;              // 任务队列对象指针（聚合关系）

    // 8. 线程池规模控制参数
    int m_minNum;   // 核心线程数（无论多闲都保留）
    int m_maxNum;   // 最大线程数（无论多忙都不超过）
    int m_busyNum;  // 当前正在执行任务的线程数
    int m_liveNum;  // 当前存活的线程总数（忙+空闲）
    int m_exitNum;  // 需要销毁的线程数（管理者->工作线程的指令）
    
    // 9. 常量与标志位
    static const int NUMBER = 2;  // 每次扩容/缩容的线程数
    bool m_shutdown = false;  // 线程池关闭标志，true时开始清理
};

