#include <atomic>
#include <array>
#include <optional>
#include <span>
#include <thread>
#include <sstream>
#include "timer.hpp"

// 性能统计开关
#ifndef QUEUE_PERF_STATS
#define QUEUE_PERF_STATS 1
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
    // 性能统计计数器
    alignas(64) struct Stats {
        std::atomic<size_t> push_attempts{0};
        std::atomic<size_t> push_success{0};
        std::atomic<size_t> push_spins{0};
        std::atomic<size_t> push_failures{0};
        
        std::atomic<size_t> pop_attempts{0};
        std::atomic<size_t> pop_success{0};
        std::atomic<size_t> pop_empty{0};
        
        std::atomic<size_t> read_at_attempts{0};
        std::atomic<size_t> read_at_success{0};

        // 耗时统计，使用计数器值
        std::atomic<uint64_t> push_total_ticks{0};    // 总耗时(计数器值)
        std::atomic<uint64_t> pop_total_ticks{0};     // 总耗时(计数器值)
        std::atomic<uint64_t> read_total_ticks{0};    // 总耗时(计数器值)
        
        std::atomic<uint64_t> push_max_ticks{0};      // 最大耗时(计数器值)
        std::atomic<uint64_t> pop_max_ticks{0};       // 最大耗时(计数器值)
        std::atomic<uint64_t> read_max_ticks{0};      // 最大耗时(计数器值)
        
        std::atomic<uint64_t> push_min_ticks{UINT64_MAX}; // 最小耗时(计数器值)
        std::atomic<uint64_t> pop_min_ticks{UINT64_MAX};  // 最小耗时(计数器值)
        std::atomic<uint64_t> read_min_ticks{UINT64_MAX}; // 最小耗时(计数器值)
    } stats_;
#endif

public:
    /**
     * @brief 构造函数,初始化缓冲区
     * 
     * std::array 会自动零初始化所有元素，但我们仍然需要
     * 正确初始化 std::atomic 对象
     */
    LockFreeRingQueue() {
        // 初始化所有槽位为空指针
        for (auto& slot : buffer_) {
            slot.store(nullptr, std::memory_order_relaxed);
        }
    }

    /**
     * @brief 析构函数，清理未被消费的数据
     */
    ~LockFreeRingQueue() {
        // 清理所有未被消费的数据
        for (auto& slot : buffer_) {
            T* ptr = slot.load(std::memory_order_relaxed);
            if (ptr) {
                delete ptr;
            }
        }
    }

    // 禁用拷贝构造和赋值操作
    LockFreeRingQueue(const LockFreeRingQueue&) = delete;
    LockFreeRingQueue& operator=(const LockFreeRingQueue&) = delete;

    /**
     * @brief 向队列推入元素
     * @param value 要推入的值
     * @return 是否推入成功
     */
    bool push(T value) {
#if QUEUE_PERF_STATS
        const auto start_time = HighResolutionTimer::now();
        stats_.push_attempts++;
        size_t spin_count = 0;
#endif

        size_t current_write = write_index_.load(std::memory_order_relaxed);
        size_t next_write = (current_write + 1) % Capacity;
        T* new_data = new T(std::move(value));

        // 阻塞写操作直到有可用空间
        while (next_write == read_index_.load(std::memory_order_acquire)) {
#if QUEUE_PERF_STATS
            spin_count++;
#endif
            std::this_thread::yield();
        }

        // 使用 compare_exchange_strong 确保写入操作的可靠性
        // 在这里使用 strong 版本是因为：
        // 1. 这是队列的关键写入操作，我们不能容忍虚假失败
        // 2. 失败代价较高（需要删除新分配的内存）
        // 3. 在大多数平台上，CAS操作都是直接映射到硬件原语，性能差异不大
        T* expected = nullptr;
        bool success = buffer_[current_write].compare_exchange_strong(
            expected, new_data, std::memory_order_release, std::memory_order_relaxed);
            
        if (success) {
            write_index_.store(next_write, std::memory_order_release);
#if QUEUE_PERF_STATS
            stats_.push_success++;
            stats_.push_spins += spin_count;
#endif
        } else {
            delete new_data;
#if QUEUE_PERF_STATS
            stats_.push_failures++;
#endif
        }

#if QUEUE_PERF_STATS
        const auto end_time = HighResolutionTimer::now();
        const auto duration = end_time - start_time;
        stats_.push_total_ticks += duration;
        
        // 使用 compare_exchange_weak 更新最大值
        // 在这里使用 weak 版本是因为：
        // 1. 这只是性能统计，可以容忍偶尔的虚假失败
        // 2. 使用循环重试，最终会成功
        // 3. 在某些平台上，weak 版本可能性能更好
        // 4. 失败的代价很低（只是重试一次统计操作）
        uint64_t current_max = stats_.push_max_ticks.load();
        while(duration > current_max && 
              !stats_.push_max_ticks.compare_exchange_weak(current_max, duration));
        
        // 同样使用 weak 版本更新最小值
        uint64_t current_min = stats_.push_min_ticks.load();
        while(duration < current_min && 
              !stats_.push_min_ticks.compare_exchange_weak(current_min, duration));
#endif
        return success;
    }

    /**
     * @brief 从队列弹出元素
     * @return 弹出的元素,如果队列为空则返回nullopt
     */
    std::optional<T> pop() {
#if QUEUE_PERF_STATS
        const auto start_time = HighResolutionTimer::now();
        stats_.pop_attempts++;
#endif

        size_t current_read = read_index_.load(std::memory_order_relaxed);
        // 检查队列是否为空
        if (current_read == write_index_.load(std::memory_order_acquire)) {
#if QUEUE_PERF_STATS
            stats_.pop_empty++;
#endif
            return std::nullopt; // 队列为空
        }

        // 尝试原子地读取并清除数据
        T* data = buffer_[current_read].load(std::memory_order_acquire);
        if (data && buffer_[current_read].compare_exchange_strong(
                data, nullptr, std::memory_order_release, std::memory_order_relaxed)) {
            read_index_.store((current_read + 1) % Capacity, std::memory_order_release);
#if QUEUE_PERF_STATS
            stats_.pop_success++;
#endif
            auto result = std::optional<T>(*data);
            delete data;

#if QUEUE_PERF_STATS
            const auto end_time = HighResolutionTimer::now();
            const auto duration = end_time - start_time;
            stats_.pop_total_ticks += duration;
            
            // 使用 compare_exchange_weak 更新最大值
            // 在这里使用 weak 版本是因为：
            // 1. 这只是性能统计，可以容忍偶尔的虚假失败
            // 2. 使用循环重试，最终会成功
            // 3. 在某些平台上，weak 版本可能性能更好
            // 4. 失败的代价很低（只是重试一次统计操作）
            uint64_t current_max = stats_.pop_max_ticks.load();
            while(duration > current_max && 
                  !stats_.pop_max_ticks.compare_exchange_weak(current_max, duration));
            
            // 同样使用 weak 版本更新最小值
            uint64_t current_min = stats_.pop_min_ticks.load();
            while(duration < current_min && 
                  !stats_.pop_min_ticks.compare_exchange_weak(current_min, duration));
#endif
            return result;
        }
        return std::nullopt;
    }

    /**
     * @brief 读取指定位置的元素(不移除)
     * @param index 相对于当前读取位置的偏移量
     * @return 读取的元素,如果位置无效则返回nullopt
     */
    std::optional<T> read_at(size_t index) {
#if QUEUE_PERF_STATS
        const auto start_time = HighResolutionTimer::now();
        stats_.read_at_attempts++;
#endif

        if (index >= Capacity) {
            return std::nullopt;
        }

        size_t current_read = read_index_.load(std::memory_order_acquire);
        size_t target_index = (current_read + index) % Capacity;
        T* data = buffer_[target_index].load(std::memory_order_acquire);

#if QUEUE_PERF_STATS
        if (data) stats_.read_at_success++;
#endif

        return data ? std::optional<T>(*data) : std::nullopt;

#if QUEUE_PERF_STATS
        const auto end_time = HighResolutionTimer::now();
        const auto duration = end_time - start_time;
        stats_.read_total_ticks += duration;
        
        // 使用 compare_exchange_weak 更新最大值
        // 在这里使用 weak 版本是因为：
        // 1. 这只是性能统计，可以容忍偶尔的虚假失败
        // 2. 使用循环重试，最终会成功
        // 3. 在某些平台上，weak 版本可能性能更好
        // 4. 失败的代价很低（只是重试一次统计操作）
        uint64_t current_max = stats_.read_max_ticks.load();
        while(duration > current_max && 
              !stats_.read_max_ticks.compare_exchange_weak(current_max, duration));
        
        // 同样使用 weak 版本更新最小值
        uint64_t current_min = stats_.read_min_ticks.load();
        while(duration < current_min && 
              !stats_.read_min_ticks.compare_exchange_weak(current_min, duration));
#endif
    }

#if QUEUE_PERF_STATS
    /**
     * @brief 获取性能统计信息
     * @return 格式化的统计信息字符串
     */
    std::string get_stats() const {
        std::stringstream ss;
        ss << "队列性能统计:\n";
        
        // Push 操作统计
        ss << "\nPush 操作统计:\n";
        const auto push_count = stats_.push_attempts.load();
        ss << "  尝试次数: " << push_count << "\n";
        ss << "  成功次数: " << stats_.push_success.load() << "\n";
        ss << "  失败次数: " << stats_.push_failures.load() << "\n";
        ss << "  自旋次数: " << stats_.push_spins.load() << "\n";
        if (push_count > 0) {
            const auto avg_ns = HighResolutionTimer::to_ns(stats_.push_total_ticks.load() / push_count);
            const auto max_ns = HighResolutionTimer::to_ns(stats_.push_max_ticks.load());
            const auto min_ns = HighResolutionTimer::to_ns(stats_.push_min_ticks.load());
            ss << "  平均耗时: " << avg_ns << " ns\n";
            ss << "  最大耗时: " << max_ns << " ns\n";
            ss << "  最小耗时: " << min_ns << " ns\n";
        }
        
        // Pop 操作统计
        ss << "\nPop 操作统计:\n";
        const auto pop_count = stats_.pop_attempts.load();
        ss << "  尝试次数: " << pop_count << "\n";
        ss << "  成功次数: " << stats_.pop_success.load() << "\n";
        ss << "  空队列次数: " << stats_.pop_empty.load() << "\n";
        if (pop_count > 0) {
            const auto avg_ns = HighResolutionTimer::to_ns(stats_.pop_total_ticks.load() / pop_count);
            const auto max_ns = HighResolutionTimer::to_ns(stats_.pop_max_ticks.load());
            const auto min_ns = HighResolutionTimer::to_ns(stats_.pop_min_ticks.load());
            ss << "  平均耗时: " << avg_ns << " ns\n";
            ss << "  最大耗时: " << max_ns << " ns\n";
            ss << "  最小耗时: " << min_ns << " ns\n";
        }
        
        // Read_at 操作统计
        ss << "\nRead_at 操作统计:\n";
        const auto read_count = stats_.read_at_attempts.load();
        ss << "  尝试次数: " << read_count << "\n";
        ss << "  成功次数: " << stats_.read_at_success.load() << "\n";
        if (read_count > 0) {
            const auto avg_ns = HighResolutionTimer::to_ns(stats_.read_total_ticks.load() / read_count);
            const auto max_ns = HighResolutionTimer::to_ns(stats_.read_max_ticks.load());
            const auto min_ns = HighResolutionTimer::to_ns(stats_.read_min_ticks.load());
            ss << "  平均耗时: " << avg_ns << " ns\n";
            ss << "  最大耗时: " << max_ns << " ns\n";
            ss << "  最小耗时: " << min_ns << " ns\n";
        }

        return ss.str();
    }

    /**
     * @brief 重置性能统计计数器
     */
    void reset_stats() {
        // 重置所有计数器
        stats_.push_attempts = 0;
        stats_.push_success = 0;
        stats_.push_spins = 0;
        stats_.push_failures = 0;
        stats_.push_total_ticks = 0;
        stats_.push_max_ticks = 0;
        stats_.push_min_ticks = UINT64_MAX;
        
        stats_.pop_attempts = 0;
        stats_.pop_success = 0;
        stats_.pop_empty = 0;
        stats_.pop_total_ticks = 0;
        stats_.pop_max_ticks = 0;
        stats_.pop_min_ticks = UINT64_MAX;
        
        stats_.read_at_attempts = 0;
        stats_.read_at_success = 0;
        stats_.read_total_ticks = 0;
        stats_.read_max_ticks = 0;
        stats_.read_min_ticks = UINT64_MAX;
    }
#endif
};