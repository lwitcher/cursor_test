#pragma once
#include <atomic>
#include <thread>
#include <functional>
#include "timer.hpp"
#include "queue.hpp"

/**
 * @brief 队列生产者性能统计类
 */
#if QUEUE_PRODUCER_PERF_STATS
class ProducerStats {
public:
    void record_produce_attempt() {
        produce_attempts++;
    }

    void record_produce_success(uint64_t start_time) {
        const auto end_time = HighResolutionTimer::now();
        const auto duration = end_time - start_time;
        
        successful_produces++;
        total_ticks += duration;
        update_max_min_time(duration);
    }

    void record_queue_full() {
        queue_full_count++;
    }

    void record_backoff() {
        backoff_count++;
    }

    std::string get_stats() const {
        std::stringstream ss;
        ss << "生产者性能统计:\n";
        
        const auto total = produce_attempts.load();
        const auto success = successful_produces.load();
        const auto full = queue_full_count.load();
        
        ss << "总生产尝试次数: " << total << "\n";
        ss << "成功生产次数: " << success << "\n";
        ss << "队列满次数: " << full << "\n";
        ss << "回退次数: " << backoff_count.load() << "\n";
        
        if (success > 0) {
            const auto avg_ns = HighResolutionTimer::to_ns(total_ticks.load() / success);
            const auto max_ns = HighResolutionTimer::to_ns(max_ticks.load());
            const auto min_ns = HighResolutionTimer::to_ns(min_ticks.load());
            
            ss << "平均生产耗时: " << avg_ns << " ns\n";
            ss << "最大生产耗时: " << max_ns << " ns\n";
            ss << "最小生产耗时: " << min_ns << " ns\n";
        }
        
        return ss.str();
    }

    void reset() {
        produce_attempts = 0;
        successful_produces = 0;
        queue_full_count = 0;
        backoff_count = 0;
        total_ticks = 0;
        max_ticks = 0;
        min_ticks = UINT64_MAX;
    }

private:
    std::atomic<size_t> produce_attempts{0};    // 生产尝试次数
    std::atomic<size_t> successful_produces{0}; // 成功生产次数
    std::atomic<size_t> queue_full_count{0};    // 队列满次数
    std::atomic<size_t> backoff_count{0};       // 回退次数
    std::atomic<uint64_t> total_ticks{0};       // 总耗时
    std::atomic<uint64_t> max_ticks{0};         // 最大耗时
    std::atomic<uint64_t> min_ticks{UINT64_MAX}; // 最小耗时

    void update_max_min_time(uint64_t duration) {
        uint64_t current_max = max_ticks.load();
        while(duration > current_max && 
              !max_ticks.compare_exchange_weak(current_max, duration));
              
        uint64_t current_min = min_ticks.load();
        while(duration < current_min && 
              !min_ticks.compare_exchange_weak(current_min, duration));
    }
};
#endif

/**
 * @brief 高性能无锁队列生产者
 * @tparam T 数据类型
 * @tparam Capacity 队列容量
 */
template<typename T, size_t Capacity>
class LockFreeQueueProducer {
private:
    LockFreeRingQueue<T, Capacity>& queue_;    // 目标队列
    std::atomic<bool> running_{false};         // 运行状态标志
    std::thread producer_thread_;              // 生产者线程
    std::function<void()> on_queue_full_;      // 队列满时的回调函数
    std::function<T()> data_generator_;        // 数据生成器

#if QUEUE_PRODUCER_PERF_STATS
    ProducerStats stats_;  // 性能统计
#endif

    /**
     * @brief 生产者线程主函数
     */
    void produce() {
        unsigned int backoff = 1;    // 初始回退值
        bool was_full = false;      // 上次是否队列满

        while (running_.load(std::memory_order_relaxed)) {
#if QUEUE_PRODUCER_PERF_STATS
            const auto start_time = HighResolutionTimer::now();
            stats_.record_produce_attempt();
#endif

            T data = data_generator_();  // 生成新数据
            if (queue_.push(std::move(data))) {
#if QUEUE_PRODUCER_PERF_STATS
                stats_.record_produce_success(start_time);
#endif
                backoff = 1;  // 重置回退值
                was_full = false;
            } else {
#if QUEUE_PRODUCER_PERF_STATS
                stats_.record_queue_full();
#endif
                
                if (!was_full) {
                    // 第一次遇到队列满，调用回调函数
                    if (on_queue_full_) {
                        on_queue_full_();
                    }
                    was_full = true;
                    continue;
                }

#if QUEUE_PRODUCER_PERF_STATS
                stats_.record_backoff();
#endif

                // 执行回退
                for (unsigned int i = 0; i < backoff; ++i) {
                    #if defined(__x86_64__)
                        _mm_pause();
                    #elif defined(__aarch64__)
                        asm volatile("yield");
                    #endif
                }
                
                // 指数回退，但设置上限
                if (backoff < 16384) {
                    backoff *= 2;
                }
            }
        }
    }

public:
    /**
     * @brief 构造函数
     * @param queue 目标队列
     * @param data_generator 数据生成器函数
     * @param on_queue_full 队列满时的回调函数
     */
    LockFreeQueueProducer(
        LockFreeRingQueue<T, Capacity>& queue,
        std::function<T()> data_generator,
        std::function<void()> on_queue_full = nullptr)
        : queue_(queue)
        , data_generator_(std::move(data_generator))
        , on_queue_full_(std::move(on_queue_full)) {}

    /**
     * @brief 析构函数，确保线程安全停止
     */
    ~LockFreeQueueProducer() {
        stop();
    }

    /**
     * @brief 启动生产者
     */
    void start() {
        if (!running_.exchange(true)) {
            producer_thread_ = std::thread(&LockFreeQueueProducer::produce, this);
        }
    }

    /**
     * @brief 停止生产者
     */
    void stop() {
        if (running_.exchange(false)) {
            if (producer_thread_.joinable()) {
                producer_thread_.join();
            }
        }
    }

#if QUEUE_PRODUCER_PERF_STATS
    /**
     * @brief 获取性能统计信息
     */
    std::string get_stats() const {
        return stats_.get_stats();
    }

    /**
     * @brief 重置统计信息
     */
    void reset_stats() {
        stats_.reset();
    }
#endif
}; 