#include <atomic>
#include <memory>
#include <vector>
#include <optional>
#include <span>
#include <thread>
#include <sstream>

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
    alignas(64) std::vector<std::atomic<T*>> buffer_; // 存储数据的环形缓冲区
    alignas(64) std::atomic<size_t> read_index_{0};   // 读取位置索引
    alignas(64) std::atomic<size_t> write_index_{0};  // 写入位置索引

#if QUEUE_PERF_STATS
    // 性能统计计数器
    alignas(64) struct Stats {
        std::atomic<size_t> push_attempts{0};
        std::atomic<size_t> push_success{0};
        std::atomic<size_t> push_spins{0};
        std::atomic<size_t> push_failures{0};
        std::atomic<uint64_t> push_total_time_ns{0};
        
        std::atomic<size_t> pop_attempts{0};
        std::atomic<size_t> pop_success{0};
        std::atomic<size_t> pop_empty{0};
        std::atomic<uint64_t> pop_total_time_ns{0};
        
        std::atomic<size_t> read_at_attempts{0};
        std::atomic<size_t> read_at_success{0};
        std::atomic<uint64_t> read_at_total_time_ns{0};
    } stats_;
#endif

public:
    /**
     * @brief 构造函数,初始化缓冲区
     */
    LockFreeRingQueue() : buffer_(Capacity) {
        // 初始化所有槽位为空指针
        for (auto& slot : buffer_) {
            slot.store(nullptr, std::memory_order_relaxed);
        }
    }

    /**
     * @brief 向队列推入元素
     * @param value 要推入的值
     * @return 是否推入成功
     */
    bool push(T value) {
#if QUEUE_PERF_STATS
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

        // 创建一个指针变量作为比较值
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
        return success;
    }

    /**
     * @brief 从队列弹出元素
     * @return 弹出的元素,如果队列为空则返回nullopt
     */
    std::optional<T> pop() {
#if QUEUE_PERF_STATS
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
    }

#if QUEUE_PERF_STATS
    /**
     * @brief 获取性能统计信息
     * @return 格式化的统计信息字符串
     */
    std::string get_stats() const {
        std::stringstream ss;
        ss << "队列性能统计:\n";
        
        // Push 统计
        ss << "\nPush 操作统计:\n";
        ss << "  尝试次数: " << stats_.push_attempts.load() << "\n";
        ss << "  成功次数: " << stats_.push_success.load() << "\n";
        ss << "  失败次数: " << stats_.push_failures.load() << "\n";
        ss << "  自旋次数: " << stats_.push_spins.load() << "\n";
        double push_avg_time = stats_.push_attempts.load() > 0 
            ? static_cast<double>(stats_.push_total_time_ns.load()) / stats_.push_attempts.load() 
            : 0.0;
        ss << "  平均耗时: " << push_avg_time << " ns\n";
        
        // Pop 统计
        ss << "\nPop 操作统计:\n";
        ss << "  尝试次数: " << stats_.pop_attempts.load() << "\n";
        ss << "  成功次数: " << stats_.pop_success.load() << "\n";
        ss << "  空队列次数: " << stats_.pop_empty.load() << "\n";
        double pop_avg_time = stats_.pop_attempts.load() > 0 
            ? static_cast<double>(stats_.pop_total_time_ns.load()) / stats_.pop_attempts.load() 
            : 0.0;
        ss << "  平均耗时: " << pop_avg_time << " ns\n";
        
        // Read_at 统计
        ss << "\nRead_at 操作统计:\n";
        ss << "  尝试次数: " << stats_.read_at_attempts.load() << "\n";
        ss << "  成功次数: " << stats_.read_at_success.load() << "\n";
        double read_at_avg_time = stats_.read_at_attempts.load() > 0 
            ? static_cast<double>(stats_.read_at_total_time_ns.load()) / stats_.read_at_attempts.load() 
            : 0.0;
        ss << "  平均耗时: " << read_at_avg_time << " ns\n";

        return ss.str();
    }

    /**
     * @brief 重置性能统计计数器
     */
    void reset_stats() {
        stats_.push_attempts = 0;
        stats_.push_success = 0;
        stats_.push_spins = 0;
        stats_.push_failures = 0;
        stats_.push_total_time_ns = 0;
        
        stats_.pop_attempts = 0;
        stats_.pop_success = 0;
        stats_.pop_empty = 0;
        stats_.pop_total_time_ns = 0;
        
        stats_.read_at_attempts = 0;
        stats_.read_at_success = 0;
        stats_.read_at_total_time_ns = 0;
    }
#endif
};