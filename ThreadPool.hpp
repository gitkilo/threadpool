//
//  ThreadPool.hpp
//  cpp
//
//  Created by 池锋良 on 2020/5/1.
//  Copyright © 2020 池锋良. All rights reserved.
//

#ifndef ThreadPool_hpp
#define ThreadPool_hpp

#include <vector>
#include <queue>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <future>
#include <functional>
#include <stdexcept>

class ThreadPool {
public:
    ThreadPool(size_t);
    template<class F, class... Args>
    auto enqueue(F&& f, Args&&... args)-> std::future<typename std::result_of<F(Args...)>::type>;
    ~ThreadPool();
private:
    // need to keep track of threads so we can join them
    std::vector< std::thread > workers;
    // the task queue
    std::queue< std::function<void()> > tasks;
    
    // recursive_mutex 允许一个线程多次调用lock, 效率比mutex要低一点
//    std::recursive_mutex mutex;
    // synchronization，独占式互斥锁，
    std::mutex queue_mutex;
    std::condition_variable condition;
    bool stop;
};
 
// the constructor just launches some amount of workers
inline ThreadPool::ThreadPool(size_t threads)
    :   stop(false)
{
    for(size_t i = 0; i < threads; ++i)
        workers.emplace_back(
            [this]
            {
                for(;;)
                {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(this->queue_mutex);
                        this->condition.wait(lock,
                                             // 如果下行返回false，将解锁lock，并堵塞在本行
                                             // 等到其他线程调用notify_one行数
                            [this]{ return this->stop || !this->tasks.empty(); }
                                             );
                        if(this->stop && this->tasks.empty())
                            return;
                        task = std::move(this->tasks.front());
                        this->tasks.pop();
                    }
                    task();
                }
            }
        );
}

void* func()
{
    std::cout<<"func"<<std::endl;
    return nullptr;
}
// add new work item to the pool
/**
 * result_of可以推断函数F(Args...)的返回值类型
 * auto ->表明函数返回类型为std::future<typename std::result_of<F(Args...)>::type>
 * 后置类型返回语法：auto GetSth(int in)->int{}
 */
template<class F, class... Args>
auto ThreadPool::enqueue(F&& f, Args&&... args)
    -> std::future<typename std::result_of<F(Args...)>::type>
{
    using return_type = typename std::result_of<F(Args...)>::type;
    
    // packaged_task, 如果直接调用如函数调用一样，并不开启线程
    /**
     *  这里std::bind将函数参数一并绑定到F函数上，在后面直接使用task()即可调用（std::placeholders::_1位占位符）
     *  std::forward 完美转发
     */
    auto task = std::make_shared< std::packaged_task<return_type()> >(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );
        
    std::future<return_type> res = task->get_future();
    {
        std::unique_lock<std::mutex> lock(queue_mutex);

        // don't allow enqueueing after stopping the pool
        if(stop)
            throw std::runtime_error("enqueue on stopped ThreadPool");

        tasks.emplace([task](){ (*task)(); });
    }
    condition.notify_one();
    return res;
}

// the destructor joins all threads
inline ThreadPool::~ThreadPool()
{
    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        stop = true;
    }
    condition.notify_all();
    for(std::thread &worker: workers)
        worker.join();
}
#endif /* ThreadPool_hpp */
