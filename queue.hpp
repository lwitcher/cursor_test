#pragma once
#include <atomic>
#include <array>
#include <optional>
#include <span>
#include <thread>
#include <sstream>
#include "timer.hpp"

/**
 * @brief 队列性能统计类
 */
#if QUEUE_PERF_STATS
/**
 * @brief 队列性能统计类
 * 
 * 用于收集和统计队列操作的性能指标,包括:
 * - push操作的成功/失败次数、耗时统计
 * - pop操作的成功/失败次数、耗时统计  
 * - read_at操作的成功/失败次数、耗时统计
 */
class QueueStats {
public:
    /**
     * @brief 记录一次push尝试
     */
    void record_push_attempt() {
        push_attempts++;
    }

    /**
     * @brief 记录一次push成功,并统计耗时
     * @param start_time push操作开始时间
     */
    void record_push_success(uint64_t start_time) {
        const auto end_time = HighResolutionTimer::now();
        const auto duration = end_time - start_time;
        
        push_success++;
        push_total_ticks += duration;
        update_max_min_push_time(duration);
    }

    /**
     * @brief 记录一次push失败
     */
    void record_push_failure() {
        push_failures++;
    }

    /**
     * @brief 记录一次push自旋等待
     */
    void record_push_spin() {
        push_spins++;
    }

    /**
     * @brief 记录一次pop尝试
     */
    void record_pop_attempt() {
        pop_attempts++;
    }

    /**
     * @brief 记录一次pop成功,并统计耗时
     * @param start_time pop操作开始时间
     */
    void record_pop_success(const auto& start_time) {
        const auto end_time = HighResolutionTimer::now();
        const auto duration = end_time - start_time;
        
        pop_success++;
        pop_total_ticks += duration;
        update_max_min_pop_time(duration);
    }

    /**
     * @brief 记录一次pop空队列
     */
    void record_pop_empty() {
        pop_empty++;
    }

    /**
     * @brief 记录一次read_at尝试
     */
    void record_read_attempt() {
        read_at_attempts++;
    }

    /**
     * @brief 记录一次read_at成功,并统计耗时
     * @param start_time read_at操作开始时间
     */
    void record_read_success(uint64_t start_time) {
        const auto end_time = HighResolutionTimer::now();
        const auto duration = end_time - start_time;
        
        read_at_success++;
        read_total_ticks += duration;
        update_max_min_read_time(duration);
    }

    /**
     * @brief 获取性能统计信息的字符串表示
     * @return 包含所有性能指标的格式化字符串
     */
    std::string get_stats() const {
        std::stringstream ss;
        ss << "队列性能统计:\n\n";
        
        // Push 操作统计
        ss << "Push 操作统计:\n";
        const auto push_count = push_attempts.load();
        ss << "  尝试次数: " << push_count << "\n";
        ss << "  成功次数: " << push_success.load() << "\n";
        ss << "  失败次数: " << push_failures.load() << "\n";
        ss << "  自旋次数: " << push_spins.load() << "\n";
        if (push_count > 0) {
            const auto avg_ns = HighResolutionTimer::to_ns(push_total_ticks.load() / push_count);
            const auto max_ns = HighResolutionTimer::to_ns(push_max_ticks.load());
            const auto min_ns = HighResolutionTimer::to_ns(push_min_ticks.load());
            ss << "  平均耗时: " << avg_ns << " ns\n";
            ss << "  最大耗时: " << max_ns << " ns\n";
            ss << "  最小耗时: " << min_ns << " ns\n";
        }
        
        // Pop 操作统计
        ss << "\nPop 操作统计:\n";
        const auto pop_count = pop_attempts.load();
        ss << "  尝试次数: " << pop_count << "\n";
        ss << "  成功次数: " << pop_success.load() << "\n";
        ss << "  空队列次数: " << pop_empty.load() << "\n";
        if (pop_count > 0) {
            const auto avg_ns = HighResolutionTimer::to_ns(pop_total_ticks.load() / pop_count);
            const auto max_ns = HighResolutionTimer::to_ns(pop_max_ticks.load());
            const auto min_ns = HighResolutionTimer::to_ns(pop_min_ticks.load());
            ss << "  平均耗时: " << avg_ns << " ns\n";
            ss << "  最大耗时: " << max_ns << " ns\n";
            ss << "  最小耗时: " << min_ns << " ns\n";
        }
        
        // Read_at 操作统计
        ss << "\nRead_at 操作统计:\n";
        const auto read_count = read_at_attempts.load();
        ss << "  尝试次数: " << read_count << "\n";
        ss << "  成功次数: " << read_at_success.load() << "\n";
        if (read_count > 0) {
            const auto avg_ns = HighResolutionTimer::to_ns(read_total_ticks.load() / read_count);
            const auto max_ns = HighResolutionTimer::to_ns(read_max_ticks.load());
            const auto min_ns = HighResolutionTimer::to_ns(read_min_ticks.load());
            ss << "  平均耗时: " << avg_ns << " ns\n";
            ss << "  最大耗时: " << max_ns << " ns\n";
            ss << "  最小耗时: " << min_ns << " ns\n";
        }

        return ss.str();
    }

    /**
     * @brief 重置所有统计计数器
     */
    void reset() {
        push_attempts = 0;
        push_success = 0;
        push_spins = 0;
        push_failures = 0;
        push_total_ticks = 0;
        push_max_ticks = 0;
        push_min_ticks = UINT64_MAX;
        
        pop_attempts = 0;
        pop_success = 0;
        pop_empty = 0;
        pop_total_ticks = 0;
        pop_max_ticks = 0;
        pop_min_ticks = UINT64_MAX;
        
        read_at_attempts = 0;
        read_at_success = 0;
        read_total_ticks = 0;
        read_max_ticks = 0;
        read_min_ticks = UINT64_MAX;
    }

private:
    // Push 操作相关的原子计数器
    std::atomic<size_t> push_attempts{0};    // push尝试次数
    std::atomic<size_t> push_success{0};     // push成功次数
    std::atomic<size_t> push_spins{0};       // push自旋次数
    std::atomic<size_t> push_failures{0};    // push失败次数
    std::atomic<uint64_t> push_total_ticks{0}; // push总耗时
    std::atomic<uint64_t> push_max_ticks{0};   // push最大耗时
    std::atomic<uint64_t> push_min_ticks{UINT64_MAX}; // push最小耗时
    
    // Pop 操作相关的原子计数器
    std::atomic<size_t> pop_attempts{0};     // pop尝试次数
    std::atomic<size_t> pop_success{0};      // pop成功次数
    std::atomic<size_t> pop_empty{0};        // 队列为空的次数
    std::atomic<uint64_t> pop_total_ticks{0};  // pop总耗时
    std::atomic<uint64_t> pop_max_ticks{0};    // pop最大耗时
    std::atomic<uint64_t> pop_min_ticks{UINT64_MAX};  // pop最小耗时
    
    // Read_at 操作相关的原子计数器
    std::atomic<size_t> read_at_attempts{0};   // read_at尝试次数
    std::atomic<size_t> read_at_success{0};    // read_at成功次数
    std::atomic<uint64_t> read_total_ticks{0}; // read_at总耗时
    std::atomic<uint64_t> read_max_ticks{0};   // read_at最大耗时
    std::atomic<uint64_t> read_min_ticks{UINT64_MAX}; // read_at最小耗时

    /**
     * @brief 更新push操作的最大最小耗时
     * @param duration 本次操作耗时
     */
    void update_max_min_push_time(uint64_t duration) {
        uint64_t current_max = push_max_ticks.load();
        while(duration > current_max && 
              !push_max_ticks.compare_exchange_weak(current_max, duration));
              
        uint64_t current_min = push_min_ticks.load();
        while(duration < current_min && 
              !push_min_ticks.compare_exchange_weak(current_min, duration));
    }

    /**
     * @brief 更新pop操作的最大最小耗时
     * @param duration 本次操作耗时
     */
    void update_max_min_pop_time(uint64_t duration) {
        uint64_t current_max = pop_max_ticks.load();
        while(duration > current_max && 
              !pop_max_ticks.compare_exchange_weak(current_max, duration));
              
        uint64_t current_min = pop_min_ticks.load();
        while(duration < current_min && 
              !pop_min_ticks.compare_exchange_weak(current_min, duration));
    }

    /**
     * @brief 更新read_at操作的最大最小耗时
     * @param duration 本次操作耗时
     */
    void update_max_min_read_time(uint64_t duration) {
        uint64_t current_max = read_max_ticks.load();
        while(duration > current_max && 
              !read_max_ticks.compare_exchange_weak(current_max, duration));
              
        uint64_t current_min = read_min_ticks.load();
        while(duration < current_min && 
              !read_min_ticks.compare_exchange_weak(current_min, duration));
    }
};
#endif

/**
 * @brief 无锁环形队列实现
 * @tparam T 队列元素类型
 * @tparam Capacity 队列容量
 */
template<typename T, size_t Capacity>
class LockFreeRingQueue {
    // 使用64字节对齐以避免伪共享
    alignas(64) std::array<std::atomic<T*>, Capacity> buffer_{}; // 使用固定大小数组存储数据
    alignas(64) std::atomic<size_t> read_index_{0};   // 读取位置索引
    alignas(64) std::atomic<size_t> write_index_{0};  // 写入位置索引

#if QUEUE_PERF_STATS
    alignas(64) QueueStats stats_;  // 性能统计
#endif

public:
    /**
     * @brief 构造函数,初始化缓冲区
     * 
     * std::array 会自动零初始化所有元素，但我们仍然需要
     * 正确初始化 std::atomic 对象
     */
    LockFreeRingQueue() {
        for (auto& slot : buffer_) {
        // 初始化所有槽位为空指针
            slot.store(nullptr, std::memory_order_relaxed);
        }
    }

    ~LockFreeRingQueue() {
        for (auto& slot : buffer_) {
            T* ptr = slot.load(std::memory_order_relaxed);
        // 清理所有未被消费的数据
            if (ptr) {
                delete ptr;
            }
        }
    }

 
    // 禁用拷贝构造和赋值操作
    LockFreeRingQueue(const LockFreeRingQueue&) = delete;
    LockFreeRingQueue& operator=(const LockFreeRingQueue&) = delete;

    bool push(T value) {
#if QUEUE_PERF_STATS
        const auto start_time = HighResolutionTimer::now();
        stats_.record_push_attempt();
#endif

        size_t current_write = write_index_.load(std::memory_order_relaxed);
        size_t next_write = (current_write + 1) % Capacity;
        
        // 检查队列是否已满
        if (next_write == read_index_.load(std::memory_order_acquire)) {
#if QUEUE_PERF_STATS
            stats_.record_push_failure();
#endif
            return false;
        }

        // 分配新的数据节点
        T* new_data = nullptr;
        try {
            new_data = new T(std::move(value));
        } catch (...) {
#if QUEUE_PERF_STATS
            stats_.record_push_failure();
#endif
            return false;
        }

        // 使用 compare_exchange_strong 确保写入操作的可靠性
        T* expected = nullptr;
        // 在这里使用 strong 版本是因为：
        // 1. 这是队列的关键写入操作，我们不能容忍虚假失败
        // 2. 失败代价较高（需要删除新分配的内存）
        // 3. 在大多数平台上，CAS操作都是直接映射到硬件原语，性能差异不大
        bool success = buffer_[current_write].compare_exchange_strong(
            expected, new_data, std::memory_order_release, std::memory_order_relaxed);

        if (success) {
            write_index_.store(next_write, std::memory_order_release);
#if QUEUE_PERF_STATS
            stats_.record_push_success(start_time);
#endif
        } else {
            delete new_data;
#if QUEUE_PERF_STATS
            stats_.record_push_failure();
#endif
        }

        return success;
    }

    /**
     * @brief 从队列中弹出数据
     * @return 弹出的数据，如果队列为空则返回std::nullopt
     */
    std::optional<T> pop() {
#if QUEUE_PERF_STATS
        const auto start_time = HighResolutionTimer::now();
        stats_.record_pop_attempt();
#endif

        size_t current_read = read_index_.load(std::memory_order_relaxed);
        
        // 检查队列是否为空
        if (current_read == write_index_.load(std::memory_order_acquire)) {
#if QUEUE_PERF_STATS
            stats_.record_pop_empty();
#endif
            return std::nullopt;
        }

        T* data = buffer_[current_read].exchange(nullptr, std::memory_order_acquire);
        if (!data) {
#if QUEUE_PERF_STATS
            stats_.record_pop_empty();
#endif
            return std::nullopt;
        }

        read_index_.store((current_read + 1) % Capacity, std::memory_order_release);
        
        std::optional<T> result{std::move(*data)};
        delete data;

#if QUEUE_PERF_STATS
        stats_.record_pop_success(start_time);
#endif

        return result;
    }

    /**
     * @brief 读取指定位置的数据但不移除
     * @brief 读取指定位置的元素(不移除)
     * @param index 相对于当前读取位置的偏移量
     * @return 读取的元素,如果位置无效则返回nullopt
     */
    std::optional<T> read_at(size_t index) {
#if QUEUE_PERF_STATS
        const auto start_time = HighResolutionTimer::now();
        stats_.record_read_attempt();
#endif

        if (index >= Capacity) {
            return std::nullopt;
        }

        size_t current_read = read_index_.load(std::memory_order_acquire);
        size_t target_index = (current_read + index) % Capacity;
        T* data = buffer_[target_index].load(std::memory_order_acquire);

#if QUEUE_PERF_STATS
        if (data) {
            stats_.record_read_success(start_time);
        }
#endif

        return data ? std::optional<T>(*data) : std::nullopt;
    }

#if QUEUE_PERF_STATS
    /**
     * @brief 获取性能统计信息
     */
    std::string get_stats() const {
        return stats_.get_stats();
    }

    /**
     * @brief 重置性能统计计数器
     */
    void reset_stats() {
        stats_.reset();
    }
#endif
};