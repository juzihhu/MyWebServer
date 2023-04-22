#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <assert.h>

#include <functional>
#include <mutex>
#include <queue>
#include <semaphore.h>
#include <thread>
class Semaphore
{
public:
    Semaphore()
    {
        if (sem_init(&semaphore_, 0, 0) != 0) {
            throw std::exception();
        }
    }

    Semaphore(int count)
    {
        if (sem_init(&semaphore_, 0, count) != 0) {
            throw std::exception();
        }
    }

    ~Semaphore() { sem_destroy(&semaphore_); }

    bool wait() { return sem_wait(&semaphore_) == 0; }

    bool post() { return sem_post(&semaphore_) == 0; }

private:
    sem_t semaphore_;
};

class ThreadPool
{
public:
    explicit ThreadPool(size_t threadCount = 8) : pool_(std::make_shared<Pool>())
    {
        assert(threadCount > 0);

        // 创建threadCount个子线程
        for (size_t i = 0; i < threadCount; i++) {
            std::thread([pool = pool_] {
                while (true) {
                    std::function<void()> task;

                    {
                        // 等待任务队列非空
                        Semaphore &queue_stat = pool->queue_stat_;
                        queue_stat.wait();

                        std::unique_lock<std::mutex> lock(pool->mtx_);
                        // 如果线程池关闭且任务队列为空，则线程退出
                        // 这样能够保证所有任务执行完再退出
                        if (pool->isClosed_ && pool->tasks_.empty()) {
                            break;
                        }
                        // 取出任务队列的头部任务
                        if (!pool->tasks_.empty()) {
                            task = std::move(pool->tasks_.front());
                            pool->tasks_.pop();
                            lock.unlock();
                        }
                    }

                    // 执行任务
                    if (task) {
                        task();
                    }
                }
            }).detach(); // 线程分离
        }
    }

    ThreadPool() = default; // 无参构造函数为默认实现

    ThreadPool(ThreadPool &&) = default;

    ~ThreadPool()
    {
        if (static_cast<bool>(pool_)) {
            {
                std::lock_guard<std::mutex> lock(pool_->mtx_);
                pool_->isClosed_ = true;
            }
            pool_->queue_stat_.post(); // 唤醒所有等待的线程
        }
    }

    template <typename F, typename... Args>
    void AddTask(F &&f, Args &&...args)
    {
        {
            std::unique_lock<std::mutex> lock(pool_->mtx_);
            // 如果线程池已关闭，则不再添加新任务
            if (pool_->isClosed_) {
                return;
            }
            // 添加任务到队列
            auto task = std::bind(std::forward<F>(f), std::forward<Args>(args)...);
            pool_->tasks_.emplace(task);
        }

        // 唤醒等待的线程
        pool_->queue_stat_.post();
    }

private:
    struct Pool {
        std::mutex mtx_;                          // 互斥锁
        Semaphore queue_stat_;                    // 信号量
        bool isClosed_ = false;                   // 是否关闭
        std::queue<std::function<void()>> tasks_; // 队列（保存的是任务）
    };
    std::shared_ptr<Pool> pool_; // 池子
};

#endif // THREADPOOL_H
