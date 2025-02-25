#pragma once
#include <atomic>
#include <thread>
#include <sstream>
#include "timer.hpp"
#include "queue.hpp"

/**
 * @brief 高性能无锁队列读取器，使用CRTP模式
 * @tparam Derived 派生类类型
 * @tparam T 数据类型
 * @tparam Capacity 队列容量
 * 
 * 这是一个高性能的队列读取器，它：
 * 1. 不会从队列中移除数据
 * 2. 使用自旋等待和回退策略
 * 3. 通过CRTP实现零开销的数据处理回调
 * 4. 提供详细的性能统计
 */
template<typename Derived, typename T, size_t Capacity>
class LockFreeQueueReader {
private:
    LockFreeRingQueue<T, Capacity>& queue_;    // 被观察的队列
    std::atomic<bool> running_{false};         // 运行状态标志
    std::thread observer_thread_;              // 观察者线程
    
#if QUEUE_READER_PERF_STATS
    QueueReaderStats stats_;  // 将统计信息移到单独的类中
#endif

    /**
     * @brief 观察者线程主函数
     */
    void observe() {
        size_t current_pos = 0;     // 当前读取位置
        unsigned int backoff = 1;    // 初始回退值
        bool was_empty = false;      // 上次读取是否为空

#if QUEUE_READER_PERF_STATS
        const auto start_time = HighResolutionTimer::now();
#endif

        while (running_.load(std::memory_order_relaxed)) {
            auto result = queue_.read_at(current_pos);
            
            if (result.has_value()) {
#if QUEUE_READER_PERF_STATS
                stats_.record_successful_read(start_time);  // 使用统计类的方法
#endif
                static_cast<Derived*>(this)->on_data(*result);
                
                current_pos++;
                backoff = 1;  // 重置回退值
                was_empty = false;
            } else {
#if QUEUE_READER_PERF_STATS
                stats_.record_empty_read();  // 使用统计类的方法
#endif
                
                if (!was_empty) {
                    // 第一次遇到空位置，重新检查当前位置
                    was_empty = true;
                    continue;
                }

#if QUEUE_READER_PERF_STATS
                stats_.increment_total_reads();
#endif

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

protected:
    /**
     * @brief 构造函数
     * @param queue 要观察的队列
     */
    explicit LockFreeQueueReader(LockFreeRingQueue<T, Capacity>& queue)
        : queue_(queue) {}

public:
    /**
     * @brief 析构函数，确保线程安全停止
     */
    ~LockFreeQueueReader() {
        stop();
    }

    /**
     * @brief 启动观察者
     */
    void start() {
        if (!running_.exchange(true)) {
            observer_thread_ = std::thread(&LockFreeQueueReader::observe, this);
        }
    }

    /**
     * @brief 停止观察者
     */
    void stop() {
        if (running_.exchange(false)) {
            if (observer_thread_.joinable()) {
                observer_thread_.join();
            }
        }
    }

#if QUEUE_READER_PERF_STATS
    /**
     * @brief 获取性能统计信息
     * @return 格式化的统计信息字符串
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

/**
 * @brief 使用示例：自定义队列读取器
 */
template<typename T, size_t Capacity>
class MyQueueReader : public LockFreeQueueReader<MyQueueReader<T, Capacity>, T, Capacity> {
private:
    using Base = LockFreeQueueReader<MyQueueReader<T, Capacity>, T, Capacity>;
    friend Base;  // 允许基类访问on_data

    /**
     * @brief 数据处理函数
     * @param data 读取到的数据
     */
    void on_data(const T& data) {
        // 在这里实现你的数据处理逻辑
    }

public:
    explicit MyQueueReader(LockFreeRingQueue<T, Capacity>& queue)
        : Base(queue) {}
};

/**
 * @brief 队列读取器性能统计类
 */
#if QUEUE_READER_PERF_STATS
class QueueReaderStats {
public:
    void record_successful_read(uint64_t start_time) {
        const auto end_time = HighResolutionTimer::now();
        const auto duration = end_time - start_time;
        
        successful_reads++;
        total_ticks += duration;
        update_max_min_time(duration);
    }

    void record_empty_read() {
        empty_reads++;
        backoff_count++;
    }

    void increment_total_reads() {
        total_reads++;
    }

    std::string get_stats() const {
        std::stringstream ss;
        ss << "观察者性能统计:\n";
        
        const auto total = total_reads.load();
        const auto success = successful_reads.load();
        const auto empty = empty_reads.load();
        
        ss << "总读取次数: " << total << "\n";
        ss << "成功读取次数: " << success << "\n";
        ss << "空读取次数: " << empty << "\n";
        ss << "回退次数: " << backoff_count.load() << "\n";
        
        if (success > 0) {
            const auto avg_ns = HighResolutionTimer::to_ns(total_ticks.load() / success);
            const auto max_ns = HighResolutionTimer::to_ns(max_ticks.load());
            const auto min_ns = HighResolutionTimer::to_ns(min_ticks.load());
            
            ss << "平均读取耗时: " << avg_ns << " ns\n";
            ss << "最大读取耗时: " << max_ns << " ns\n";
            ss << "最小读取耗时: " << min_ns << " ns\n";
        }
        
        return ss.str();
    }

    void reset() {
        total_reads = 0;
        successful_reads = 0;
        empty_reads = 0;
        total_ticks = 0;
        max_ticks = 0;
        min_ticks = UINT64_MAX;
        backoff_count = 0;
    }

private:
    std::atomic<size_t> total_reads{0};
    std::atomic<size_t> successful_reads{0};
    std::atomic<size_t> empty_reads{0};
    std::atomic<uint64_t> total_ticks{0};
    std::atomic<uint64_t> max_ticks{0};
    std::atomic<uint64_t> min_ticks{UINT64_MAX};
    std::atomic<size_t> backoff_count{0};

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