#pragma once

#include <condition_variable>
#include <cstdio>
#include <exception>
#include <functional>
#include <iostream>
#include <list>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
#include <atomic>

// 任务队列
template <typename T>
class task_que {
public:
    // 队列
    std::list<std::shared_ptr<T>> tasks;
    // 任务队列的读写锁
    std::mutex task_mutex;
    // 任务队列的条件变量
    std::condition_variable cond;
};

// 线程池类
template <typename T>
class threadpool {
public:
    //参数thread_number是线程池中线程的数量，max_requests是请求队列中最多允许的、等待处理的请求的数量
    threadpool(int thread_number = 8, int max_requests = 10000);
    ~threadpool();

    //往请求队列中添加任务

    bool append(std::shared_ptr<T> request);

private:
    //工作线程运行的函数，它不断从工作队列中取出任务并执行之
    void run();

private:
    int thread_number; //线程池中的线程数
    int max_requests;  //请求队列中允许的最大请求数
    bool m_stop;       //是否结束线程

    // 保存线程池中所有线程id的数组
    std::vector<std::thread::id> pool;
    task_que<T> que;
};

template <typename T>
threadpool<T>::threadpool(int _thread_number, int _max_requests) : thread_number(_thread_number), max_requests(_max_requests), m_stop(false) {
    if ((thread_number <= 0) || (max_requests <= 0)) {
        throw std::exception();
    }

    for (int i = 0; i < thread_number; i++) {
        try {
            std::thread temp_thread_var{&threadpool::run, this};
            pool.push_back(temp_thread_var.get_id());
            temp_thread_var.detach();
            printf("created the %dth thread\n", i + 1);
        } catch (const std::exception &e) { std::cerr << e.what() << '\n'; }
    }
}

template <typename T>
threadpool<T>::~threadpool() {
    m_stop = true;
}

template <typename T>
bool threadpool<T>::append(std::shared_ptr<T> request) {
    std::lock_guard<std::mutex> lk(que.task_mutex);
    if (que.tasks.size() > max_requests) {
        return false;
    }
    que.tasks.push_back(request);
    que.cond.notify_one();
    return true;
}

template <typename T>
void threadpool<T>::run() {
    while (!m_stop) {
        std::unique_lock<std::mutex> lk(que.task_mutex);
        que.cond.wait(lk, [this] { return !this->que.tasks.empty(); });
        T *task = que.tasks.front();
        que.tasks.pop_front();
        lk.unlock();
        task->process();
    }
}
