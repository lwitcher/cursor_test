#include "queue.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <iomanip>
#include <optional>

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
        auto result = queue.pop();
        if (result.has_value()) {
            stats.pop_success++;
        } else {
            stats.pop_failure++;
            // 使用完整的命名空间限定
            ::std::this_thread::sleep_for(::std::chrono::microseconds(1));
        }
    }
}

int main() {
    // 创建队列实例
    LockFreeRingQueue<int, QUEUE_CAPACITY> queue;
    Statistics stats;

    // 记录开始时间
    auto start_time = std::chrono::high_resolution_clock::now();

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

    // 计算执行时间
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

    // 输出性能统计信息
    std::cout << "性能测试结果:\n";
    std::cout << "执行时间: " << duration.count() << " ms\n\n";
    
    std::cout << "生产者统计:\n";
    std::cout << "成功推送: " << stats.push_success << "\n";
    std::cout << "推送失败: " << stats.push_failure << "\n";
    
    std::cout << "\n消费者统计:\n";
    std::cout << "成功弹出: " << stats.pop_success << "\n";
    std::cout << "弹出失败: " << stats.pop_failure << "\n";

    // 计算每秒操作数
    double total_ops = stats.push_success + stats.pop_success;
    double ops_per_second = (total_ops * 1000.0) / duration.count();
    std::cout << "\n每秒操作数: " << std::fixed << std::setprecision(2) 
              << ops_per_second << " ops/s\n";

    return 0;
} 