#include "queue.hpp"
#include "lock_free_queue_producer.hpp"
#include "lock_free_queue_reader.hpp"
#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include <atomic>
#include <iomanip>
#include <optional>
#include <random>
#include "memory_pool.hpp"
// 添加 x86 intrinsics 头文件
#if defined(__x86_64__)
    #include <immintrin.h>
#elif defined(__aarch64__)
    #include <arm_neon.h>
#endif
#include "timer.hpp"

/**
 * @brief 测试用的自定义数据类型
 */
struct TestData {
    uint64_t timestamp;      // 时间戳
    uint64_t sequence;       // 序列号
    uint64_t value;         // 数值
    uint8_t  flags[4];      // 标志位

    // 构造函数
    TestData(uint64_t seq = 0) 
        : timestamp(0)
        , sequence(seq)
        , value(0)
    {
        flags[0] = 0;
        flags[1] = 0;
        flags[2] = 0;
        flags[3] = 0;
    }
};

/**
 * @brief 性能测试参数
 */
constexpr size_t QUEUE_CAPACITY = 20000;       // 队列容量
constexpr size_t OPERATIONS_PER_THREAD = 1000000;  // 每个线程的操作次数
constexpr size_t NUM_PRODUCERS = 2;            // 生产者线程数
constexpr size_t NUM_CONSUMERS = 3;            // 消费者线程数
constexpr size_t NUM_OPERATIONS = 1000000; // 操作次数

/**
 * @brief 数据生成器
 */
class DataGenerator {
public:
    DataGenerator() : sequence_(0) {
        // 初始化随机数生成器
        std::random_device rd;
        rng_.seed(rd());
    }

    TestData generate() {
        TestData data(sequence_++);
        
        // 生成随机时间戳
        data.timestamp = std::chrono::system_clock::now()
            .time_since_epoch()
            .count();
        
        // 生成随机值
        data.value = value_dist_(rng_);
        
        // 生成随机标志位
        for (int i = 0; i < 4; ++i) {
            data.flags[i] = flag_dist_(rng_);
        }
        
        return data;
    }

private:
    std::atomic<uint64_t> sequence_;
    std::mt19937_64 rng_;
    std::uniform_int_distribution<uint64_t> value_dist_;
    std::uniform_int_distribution<uint16_t> flag_dist_{0, 255};
};

/**
 * @brief 队列满时的回调函数
 */
void on_queue_full() {
    std::cout << "Queue is full!" << std::endl;
}

/**
 * @brief 使用内存池进行性能测试
 */
void test_memory_pool() {
    MemoryPool<TestData> pool;

    auto start = HighResolutionTimer::now();

    // 分配对象
    std::vector<TestData*> objects;
    for (size_t i = 0; i < NUM_OPERATIONS; ++i) {
        TestData* obj = pool.allocate();
        obj->sequence = i; // 设置序列号
        objects.push_back(obj);
    }

    // 释放对象
    for (auto obj : objects) {
        pool.deallocate(obj);
    }

    auto end = HighResolutionTimer::now();
    const auto duration_ms = HighResolutionTimer::to_ms(end - start);

    std::cout << "内存池: " << duration_ms << " 毫秒用于 " << NUM_OPERATIONS << " 次分配和释放。\n";
}

/**
 * @brief 使用 new 进行性能测试
 */
void test_new() {
    auto start = HighResolutionTimer::now();

    // 分配对象
    std::vector<TestData*> objects;
    for (size_t i = 0; i < NUM_OPERATIONS; ++i) {
        TestData* obj = new TestData(i);
        objects.push_back(obj);
    }

    // 释放对象
    for (auto obj : objects) {
        delete obj;
    }

    auto end = HighResolutionTimer::now();
    const auto duration_ms = HighResolutionTimer::to_ms(end - start);

    std::cout << "使用 new: " << duration_ms << " 毫秒用于 " << NUM_OPERATIONS << " 次分配和释放。\n";
}

int main() {
    // 初始化高精度计时器
    HighResolutionTimer::init();

    // 创建队列
    NBQueue<TestData, QUEUE_CAPACITY> queue;
    
    // 创建数据生成器
    DataGenerator generator;
    
    // 创建生产者
    std::vector<std::unique_ptr<LockFreeQueueProducer<TestData, QUEUE_CAPACITY>>> producers;
    for (size_t i = 0; i < NUM_PRODUCERS; ++i) {
        producers.emplace_back(std::make_unique<LockFreeQueueProducer<TestData, QUEUE_CAPACITY>>(
            queue,
            [&generator]() { return generator.generate(); },
            on_queue_full
        ));
    }

    // 创建消费者
    std::vector<std::unique_ptr<MyQueueReader<TestData, QUEUE_CAPACITY>>> consumers;
    for (size_t i = 0; i < NUM_CONSUMERS; ++i) {
        consumers.emplace_back(std::make_unique<MyQueueReader<TestData, QUEUE_CAPACITY>>(queue));
    }

    const auto start_count = HighResolutionTimer::now();

    // 启动所有生产者
    for (auto& producer : producers) {
        producer->start();
    }

    // 启动所有消费者
    for (auto& consumer : consumers) {
        consumer->start();
    }

    // 等待一段时间
    std::this_thread::sleep_for(std::chrono::seconds(10));

    // 停止所有生产者
    for (auto& producer : producers) {
        producer->stop();
    }

    // 停止所有消费者
    for (auto& consumer : consumers) {
        consumer->stop();
    }

    const auto end_count = HighResolutionTimer::now();
    const auto duration_ms = HighResolutionTimer::to_ms(end_count - start_count);

    // 输出性能统计信息
    std::cout << "\n=== 队列性能统计 ===\n";
    std::cout << queue.get_stats() << "\n";

    std::cout << "\n=== 生产者性能统计 ===\n";
    for (size_t i = 0; i < producers.size(); ++i) {
        std::cout << "\n生产者 " << i << ":\n";
        std::cout << producers[i]->get_stats() << "\n";
    }

    std::cout << "\n=== 消费者性能统计 ===\n";
    for (size_t i = 0; i < consumers.size(); ++i) {
        std::cout << "\n消费者 " << i << ":\n";
        std::cout << consumers[i]->get_stats() << "\n";
    }

    std::cout << "\n总执行时间: " << duration_ms << " 毫秒\n";

    // 测试内存池性能
    test_memory_pool();

    // 测试 new 性能
    test_new();

    return 0;
} 