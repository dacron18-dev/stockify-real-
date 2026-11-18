// Multithreading: Runnable/Thread wrappers, periodic task, thread pool.
#pragma once
#include <condition_variable>
#include <functional>
#include <future>
#include <queue>
#include <thread>

// Like Java's Runnable: anything with run().
class Runnable {
public:
    virtual ~Runnable() = default;
    virtual void run() = 0;
};

// Java-style Thread wrapper around std::thread: start(), join(), sleep(), yield().
// Life cycle: NEW (constructed) -> RUNNABLE (start) -> TIMED_WAITING (sleep/wait_for) -> TERMINATED (run returns).
class Thread {
    Runnable& task_;
    std::thread t_;
public:
    explicit Thread(Runnable& r) : task_(r) {}
    Thread(const Thread&) = delete;
    Thread& operator=(const Thread&) = delete;
    ~Thread() { join(); }                                   // RAII: never leaves a joinable thread
    void start() { t_ = std::thread([this] { task_.run(); }); }
    void join() { if (t_.joinable()) t_.join(); }
    static void sleep(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
    static void yield() { std::this_thread::yield(); }
};

// Runs a function every `ms` milliseconds until stop() is called.
class PeriodicTask : public Runnable {
    std::function<void()> fn_;
    int ms_;
    std::atomic<bool> stop_{false};
    std::mutex m_;
    std::condition_variable cv_;
public:
    PeriodicTask(std::function<void()> fn, int ms) : fn_(std::move(fn)), ms_(ms) {}
    void run() override {
        std::unique_lock<std::mutex> lk(m_);
        while (!stop_) {
            lk.unlock(); fn_(); lk.lock();                  // don't hold the lock during the work
            cv_.wait_for(lk, std::chrono::milliseconds(ms_), [this] { return stop_.load(); });
        }
    }
    void stop() { { std::lock_guard<std::mutex> g(m_); stop_ = true; } cv_.notify_all(); }
    void reset() { stop_ = false; }                         // allow restart after stop()
};

// Producer-consumer thread pool: callers produce jobs, workers consume them. submit() returns a future.
class ThreadPool {
    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> q_;
    std::mutex m_;
    std::condition_variable cv_;
    bool stop_ = false;
public:
    explicit ThreadPool(std::size_t n) {
        for (std::size_t i = 0; i < n; ++i)
            workers_.emplace_back([this] {
                for (;;) {
                    std::function<void()> job;
                    {
                        std::unique_lock<std::mutex> lk(m_);
                        cv_.wait(lk, [this] { return stop_ || !q_.empty(); });
                        if (stop_ && q_.empty()) return;
                        job = std::move(q_.front()); q_.pop();
                    }
                    job();
                }
            });
    }
    ~ThreadPool() {
        { std::lock_guard<std::mutex> g(m_); stop_ = true; }
        cv_.notify_all();
        for (auto& w : workers_) w.join();
    }
    template <typename F> auto submit(F f) -> std::future<std::invoke_result_t<F>> {
        using R = std::invoke_result_t<F>;
        auto task = std::make_shared<std::packaged_task<R()>>(std::move(f));
        auto fut = task->get_future();
        { std::lock_guard<std::mutex> g(m_); q_.push([task] { (*task)(); }); }
        cv_.notify_one();
        return fut;
    }
};
