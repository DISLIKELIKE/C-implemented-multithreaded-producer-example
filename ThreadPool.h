#ifndef THREAD_POOL_H
#define THREAD_POOL_H

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
    auto enqueue(F&& f, Args&&... args)
        -> std::future<typename std::result_of<F(Args...)>::type>;
    ~ThreadPool();
private:
    // need to keep track of threads so we can join them
    std::vector< std::thread > workers;
    // the task queue
    std::queue< std::function<void()> > tasks;

    // synchronization
    std::mutex queue_mutex;
    std::condition_variable condition;
    bool stop;
};

// the constructor just launches some amount of workers
// 当这个类的对象被创建时，它会自动初始化并启动一定数量的工作者来执行后续的任务或操作。
inline ThreadPool::ThreadPool(size_t threads)
    : stop(false)
{
    for (size_t i = 0; i < threads; ++i)
        //threads是线程池应该包含的工作线程的数量。
        workers.emplace_back(
            [this]
            //[this]捕获列表表示lambda表达式将捕获当前对象的this指针，以便在lambda内部访问当前对象的成员。
            {
                for (;;)
                    //无限循环
                {
                    std::function<void()> task;

                    {
                        std::unique_lock<std::mutex> lock(this->queue_mutex);
                        //锁定一个互斥量（queue_mutex），以保护对任务队列（tasks）的访问。

                        this->condition.wait(lock,
                            [this] { return this->stop || !this->tasks.empty(); });
                        //condition.wait方法会使线程阻塞，直到条件变量被通知，或者提供的谓词（lambda表达式）
                        // 返回true。谓词检查是否收到了停止信号（this->stop）或者任务队列是否为空（!this->tasks.empty()）。

                        if (this->stop && this->tasks.empty())
                            return;
                        //如果线程在等待过程中收到了停止信号（this->stop为true），并且任务队列已经为空，则线程会退出循环，从而结束执行。
                        task = std::move(this->tasks.front());
                        this->tasks.pop();
                    }

                    task();
                }
            }
        );
}

// add new work item to the pool
template<class F, class... Args>
auto ThreadPool::enqueue(F&& f, Args&&... args)
-> std::future<typename std::result_of<F(Args...)>::type>
{
    using return_type = typename std::result_of<F(Args...)>::type;//定义一个类型别名return_type

    auto task = std::make_shared< std::packaged_task<return_type()> >(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...)
    );

    std::future<return_type> res = task->get_future();
    {
        std::unique_lock<std::mutex> lock(queue_mutex);

        // don't allow enqueueing after stopping the pool
        if (stop)
            throw std::runtime_error("enqueue on stopped ThreadPool");

        tasks.emplace([task]() { (*task)(); });
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
    for (std::thread& worker : workers)
        worker.join();
}

#endif