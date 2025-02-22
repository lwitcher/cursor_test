#include "queue.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <iomanip>
#include <optional>
// 添加 x86 intrinsics 头文件
#if defined(__x86_64__) || defined(_M_X64)
    #include <immintrin.h>
#elif defined(__arm__) || defined(__aarch64__)
    #include <arm_neon.h>
#endif
#include "timer.hpp"

/**
 * @brief 性能测试参数
 */
constexpr size_t QUEUE_CAPACITY = 1024;        // 队列容量
constexpr size_t OPERATIONS_PER_THREAD = 1000000;  // 每个线程的操作次数
constexpr size_t NUM_PRODUCERS = 2;            // 生产者线程数
constexpr size_t NUM_CONSUMERS = 2;            // 消费者线程数

/**
 * @brief 统计计数器
 */
struct Statistics {
    std::atomic<size_t> push_success{0};
    std::atomic<size_t> push_failure{0};
    std::atomic<size_t> pop_success{0};
    std::atomic<size_t> pop_failure{0};
    std::atomic<size_t> read_success{0};
    std::atomic<size_t> read_failure{0};
};

/**
 * @brief 生产者线程函数
 * @param queue 共享队列
 * @param stats 统计数据
 * @param thread_id 线程ID
 */
void producer(LockFreeRingQueue<int, QUEUE_CAPACITY>& queue, Statistics& stats, int thread_id) {
    for (size_t i = 0; i < OPERATIONS_PER_THREAD; ++i) {
        int value = (thread_id * OPERATIONS_PER_THREAD) + i;
        if (queue.push(value)) {
            stats.push_success++;
        } else {
            stats.push_failure++;
        }
    }
}

/**
 * @brief 消费者线程函数
 * @param queue 共享队列
 * @param stats 统计数据
 */
void consumer(LockFreeRingQueue<int, QUEUE_CAPACITY>& queue, Statistics& stats) {
    for (size_t i = 0; i < OPERATIONS_PER_THREAD; ++i) {
        unsigned int backoff = 1;  // 初始回退值
        while (true) {
            auto result = queue.pop();
            if (result.has_value()) {
                stats.pop_success++;
                break;
            }
            stats.pop_failure++;
            
            // 渐进式回退策略
            for (unsigned int i = 0; i < backoff; ++i) {
                // 使用CPU的PAUSE指令，减少能耗并优化自旋等待
                #if defined(__x86_64__) || defined(_M_X64)
                    _mm_pause();  // Intel/AMD CPU
                #elif defined(__arm__) || defined(__aarch64__)
                    asm volatile("yield");  // ARM CPU
                #endif
            }
            
            // 指数回退，但设置上限，2^14 = 16384
            if (backoff < 16384) {
                backoff *= 2;
            }
        }
    }
}

/**
 * @brief 读取者线程函数 - 持续读取队列中的所有数据
 * @param queue 共享队列
 * @param stats 统计数据
 */
void reader(LockFreeRingQueue<int, QUEUE_CAPACITY>& queue, Statistics& stats) {
    size_t current_pos = 0;  // 当前读取位置
    for (size_t i = 0; i < OPERATIONS_PER_THREAD; ++i) {
        unsigned int backoff = 1;  // 初始回退值
        while (true) {
            // 尝试读取当前位置的元素
            auto result = queue.read_at(current_pos);
            if (result.has_value()) {
                stats.read_success++;
                current_pos++;  // 移动到下一个位置
                break;
            }
            stats.read_failure++;
            
            // 渐进式回退策略
            for (unsigned int j = 0; j < backoff; ++j) {
                #if defined(__x86_64__) || defined(_M_X64)
                    _mm_pause();
                #elif defined(__arm__) || defined(__aarch64__)
                    asm volatile("yield");
                #endif
            }
            
            if (backoff < 16384) {
                backoff *= 2;
            }
        }
    }
}


int main() {
    // 初始化高精度计时器
    HighResolutionTimer::init();

    LockFreeRingQueue<int, QUEUE_CAPACITY> queue;
    Statistics stats;

    const auto start_count = HighResolutionTimer::now();

    // 创建生产者和消费者线程
    std::vector<std::thread> producers;
    std::vector<std::thread> consumers;

    // 启动生产者线程
    for (size_t i = 0; i < NUM_PRODUCERS; ++i) {
        producers.emplace_back(producer, std::ref(queue), std::ref(stats), i);
    }

    // 启动消费者线程
    for (size_t i = 0; i < NUM_CONSUMERS; ++i) {
        consumers.emplace_back(consumer, std::ref(queue), std::ref(stats));
    }

    // 等待所有线程完成
    for (auto& p : producers) {
        p.join();
    }
    for (auto& c : consumers) {
        c.join();
    }

    const auto end_count = HighResolutionTimer::now();
    const auto duration_ms = HighResolutionTimer::to_ms(end_count - start_count);

    // 输出性能统计信息
    std::cout << "性能测试结果:\n";
    std::cout << "执行时间: " << duration_ms << " ms\n\n";
    
    std::cout << "生产者统计:\n";
    std::cout << "成功推送: " << stats.push_success << "\n";
    std::cout << "推送失败: " << stats.push_failure << "\n";
    
    std::cout << "\n消费者统计:\n";
    std::cout << "成功弹出: " << stats.pop_success << "\n";
    std::cout << "弹出失败: " << stats.pop_failure << "\n";

    // 计算每秒操作数
    double total_ops = stats.push_success + stats.pop_success;
    double ops_per_second = (total_ops * 1000.0) / duration_ms;
    std::cout << "\n每秒操作数: " << std::fixed << std::setprecision(2) 
              << ops_per_second << " ops/s\n";

    return 0;
} 