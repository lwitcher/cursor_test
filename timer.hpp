#pragma once
#include <chrono>

/**
 * @brief 高性能计时器类
 * 
 * 这个计时器类提供高精度的时间测量功能，根据不同的CPU架构使用不同的实现：
 * - x86_64: 使用 RDTSCP 指令读取时间戳计数器(TSC)，提供纳秒级精度
 * - ARM64: 使用 CNTVCT_EL0 寄存器读取虚拟计数器，提供纳秒级精度
 * - 其他平台: 回退到使用 std::chrono::steady_clock
 * 
 * 使用方法:
 * 1. 程序开始时调用 init() 进行初始化
 * 2. 使用 now() 获取时间戳
 * 3. 使用 to_ns/us/ms/sec 将时间戳差值转换为对应时间单位
 */
class HighResolutionTimer {
public:
    /**
     * @brief 初始化计时器
     * 
     * 在程序开始时调用一次，用于:
     * 1. 预热CPU，避免首次测量的不准确
     * 2. 校准计数器频率
     * 3. 初始化静态成员
     */
    static void init() {
        // 预热频率校准
        (void)get_freq();
    }

    /**
     * @brief 获取当前时间点的计数器值
     * 
     * 根据CPU架构使用不同的指令获取高精度计数器值:
     * - x86_64: RDTSCP指令，读取CPU的时间戳计数器
     * - ARM64: CNTVCT_EL0寄存器，读取ARM虚拟计时器
     * - 其他: 使用std::chrono::steady_clock
     * 
     * @return 当前时间点的计数器值(raw count)，无符号64位整数
     */
    static inline uint64_t now() noexcept {
        #if defined(__x86_64__)
            unsigned int aux;
            return static_cast<uint64_t>(__rdtscp(&aux));  // 读取TSC，同时防止乱序执行
        #elif defined(__aarch64__)
            uint64_t virtual_timer_value;
            asm volatile("mrs %0, cntvct_el0" : "=r"(virtual_timer_value));
            return virtual_timer_value;
        #else
            return static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
        #endif
    }

    /**
     * @brief 将计数器值转换为纳秒
     * 
     * 计算公式: ns = count * 1000000.0 / freq
     * freq为每毫秒的计数次数，所以需要乘以1000000来转换为纳秒
     * 
     * @param count 计数器值差值(end - start)
     * @return 纳秒数(double类型以保持精度)
     */
    static inline double to_ns(uint64_t count) noexcept {
        return static_cast<double>(count) * 1000000.0 / get_freq();
    }

    /**
     * @brief 将计数器值转换为微秒
     * 
     * 计算公式: us = count * 1000.0 / freq
     * freq为每毫秒的计数次数，所以需要乘以1000来转换为微秒
     * 
     * @param count 计数器值差值(end - start)
     * @return 微秒数(double类型以保持精度)
     */
    static inline double to_us(uint64_t count) noexcept {
        return static_cast<double>(count) * 1000.0 / get_freq();
    }

    /**
     * @brief 将计数器值转换为毫秒
     * 
     * 计算公式: ms = count / freq
     * freq本身就是每毫秒的计数次数，所以直接除即可
     * 
     * @param count 计数器值差值(end - start)
     * @return 毫秒数(double类型以保持精度)
     */
    static inline double to_ms(uint64_t count) noexcept {
        return static_cast<double>(count) / get_freq();
    }

    /**
     * @brief 将计数器值转换为秒
     * 
     * 计算公式: s = count / (freq * 1000.0)
     * freq为每毫秒的计数次数，需要再除以1000转换为秒
     * 
     * @param count 计数器值差值(end - start)
     * @return 秒数(double类型以保持精度)
     */
    static inline double to_sec(uint64_t count) noexcept {
        return static_cast<double>(count) / (get_freq() * 1000.0);
    }

private:
    /**
     * @brief 获取计数器频率
     * 
     * 使用单例模式缓存校准后的频率值，避免重复校准
     * 频率表示每毫秒的计数次数
     * 
     * @return 每毫秒的计数次数(cached)
     */
    static double get_freq() {
        static double freq = calibrate_freq();
        return freq;
    }

    /**
     * @brief 校准计数器频率
     * 
     * ARM64平台:
     * - 直接从CNTFRQ_EL0寄存器读取系统计数器频率
     * - 将频率转换为每毫秒的计数次数
     * 
     * 其他平台:
     * - 通过实际计时来校准频率
     * - 测量100ms内的计数次数
     * - 计算每毫秒的平均计数次数
     * 
     * @return 每毫秒的计数次数
     */
    static double calibrate_freq() {
        #if defined(__aarch64__)
            // ARM64: 读取 CNTFRQ_EL0 寄存器获取频率
            uint64_t freq;
            asm volatile("mrs %0, cntfrq_el0" : "=r"(freq));
            return static_cast<double>(freq) / 1000.0;  // 转换为每毫秒的计数次数
        #else
            // 其他平台：通过实际测量计算频率
            const auto duration = std::chrono::milliseconds(100);
            const auto start = std::chrono::steady_clock::now();
            const auto start_count = now();
            
            // 使用忙等待循环以获得更精确的时间测量
            // 不使用yield避免线程切换带来的额外开销
            while (std::chrono::steady_clock::now() - start < duration) {
                #if defined(__x86_64__)
                    _mm_pause();  // Intel CPU的暂停指令,减少能耗
                #elif defined(__aarch64__)
                    asm volatile("yield");  // ARM CPU的yield指令
                #endif
            }
            
            const auto end_count = now();
            const auto elapsed_ms = duration.count();
            return static_cast<double>(end_count - start_count) / elapsed_ms;
        #endif
    }
};