#include <atomic>
#include <memory>
#include <vector>
#include <optional>
#include <span>

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
        size_t current_write = write_index_.load(std::memory_order_relaxed);
        size_t next_write = (current_write + 1) % Capacity;
        T* new_data = new T(std::move(value));

        // 阻塞写操作直到有可用空间
        while (next_write == read_index_.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }

        // 原子写入数据
        if (buffer_[current_write].compare_exchange_strong(
                nullptr, new_data, std::memory_order_release, std::memory_order_relaxed)) {
            write_index_.store(next_write, std::memory_order_release);
            return true;
        }
        delete new_data;
        return false;
    }

    /**
     * @brief 从队列弹出元素
     * @return 弹出的元素,如果队列为空则返回nullopt
     */
    std::optional<T> pop() {
        size_t current_read = read_index_.load(std::memory_order_relaxed);
        // 检查队列是否为空
        if (current_read == write_index_.load(std::memory_order_acquire)) {
            return std::nullopt; // 队列为空
        }

        // 尝试原子地读取并清除数据
        T* data = buffer_[current_read].load(std::memory_order_acquire);
        if (data && buffer_[current_read].compare_exchange_strong(
                data, nullptr, std::memory_order_release, std::memory_order_relaxed)) {
            read_index_.store((current_read + 1) % Capacity, std::memory_order_release);
            return *data;
        }
        return std::nullopt;
    }

    /**
     * @brief 读取指定位置的元素(不移除)
     * @param index 相对于当前读取位置的偏移量
     * @return 读取的元素,如果位置无效则返回nullopt
     */
    std::optional<T> read_at(size_t index) {
        if (index >= Capacity) return std::nullopt;
        size_t current_read = read_index_.load(std::memory_order_acquire);
        size_t target_index = (current_read + index) % Capacity;
        T* data = buffer_[target_index].load(std::memory_order_acquire);
        return data ? *data : std::nullopt;
    }
};