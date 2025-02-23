#pragma once
#include <chrono>

/**
 * @brief 高性能计时器类
 * 
 * x86_64: 使用 RDTSCP 指令
 * ARM64: 使用 CNTVCT_EL0 寄存器
 */
class HighResolutionTimer {
public:
    /**
     * @brief 初始化计时器
     * 在程序开始时调用一次
     */
    static void init() {
        // 预热频率校准
        (void)get_freq();
    }

    /**
     * @brief 获取当前时间点
     * @return 当前时间点（计数器值）
     */
    static inline int64_t now() noexcept {
        #if defined(__x86_64__) || defined(_M_X64)
            unsigned int aux;
            return __rdtscp(&aux);
        #elif defined(__aarch64__)
            int64_t virtual_timer_value;
            asm volatile("mrs %0, cntvct_el0" : "=r"(virtual_timer_value));
            return virtual_timer_value;
        #else
            return std::chrono::steady_clock::now().time_since_epoch().count();
        #endif
    }

    /**
     * @brief 将计数器值转换为纳秒
     * @param count 计数器值
     * @return 纳秒数
     */
    static inline double to_ns(int64_t count) noexcept {
        return static_cast<double>(count) * 1000000.0 / get_freq();
    }

    /**
     * @brief 将计数器值转换为微秒
     * @param count 计数器值
     * @return 微秒数
     */
    static inline double to_us(int64_t count) noexcept {
        return static_cast<double>(count) * 1000.0 / get_freq();
    }

    /**
     * @brief 将计数器值转换为毫秒
     * @param count 计数器值
     * @return 毫秒数
     */
    static inline double to_ms(int64_t count) noexcept {
        return static_cast<double>(count) / get_freq();
    }

    /**
     * @brief 将计数器值转换为秒
     * @param count 计数器值
     * @return 秒数
     */
    static inline double to_sec(int64_t count) noexcept {
        return static_cast<double>(count) / (get_freq() * 1000.0);
    }

private:
    /**
     * @brief 获取计数器频率
     * @return 每毫秒的计数次数
     */
    static double get_freq() {
        static double freq = calibrate_freq();
        return freq;
    }

    /**
     * @brief 校准计数器频率
     * @return 每毫秒的计数次数
     */
    static double calibrate_freq() {
        #if defined(__aarch64__)
            // ARM64: 读取 CNTFRQ_EL0 寄存器获取频率
            uint64_t freq;
            asm volatile("mrs %0, cntfrq_el0" : "=r"(freq));
            return static_cast<double>(freq) / 1000.0;  // 转换为每毫秒
        #else
            // 其他平台：通过实际测量计算频率
            const auto duration = std::chrono::milliseconds(100);
            const auto start = std::chrono::steady_clock::now();
            const auto start_count = now();
            
            while (std::chrono::steady_clock::now() - start < duration) {
                std::this_thread::yield();
            }
            
            const auto end_count = now();
            const auto elapsed_ms = duration.count();
            return static_cast<double>(end_count - start_count) / elapsed_ms;
        #endif
    }
};