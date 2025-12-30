#include "ThreadPool.h"
#include "TaskQueue.h"
#include <iostream>
#include <unistd.h>
using namespace std;

// 1. 定义一个任务函数（线程池最终要执行的函数）
// 格式必须为：void (*)(void* arg)
void taskFunc(void* arg)
{
    int num = *(int*)arg;   // 将void*参数转换回实际类型
    cout << "thread " << to_string(pthread_self()) << " is working, number = " << num << endl;
    sleep(1);
}

int main()
{
    // 2. 创建线程池对象（最小3个线程，最大10个线程）
    ThreadPool pool(3, 10);
    // 3. 模拟生产者：向线程池添加100个任务
    for (int i = 0; i < 100; ++i)
    {
        int* num = new int; // 在堆上分配参数内存
        *num = i + 100; // 设置参数值
        // 创建Task对象，并将任务函数和参数绑定，然后添加到线程池
        pool.addTask(Task(taskFunc, num));
    }
    // 4. 主线程睡眠，等待工作线程处理任务
    sleep(30);
    return 0;
    // 5. 主函数结束，局部对象pool的析构函数被调用，开始清理线程池
}