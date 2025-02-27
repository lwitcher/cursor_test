#pragma once
#include <cstddef>
#include <vector>
#include <atomic>
#include <iostream>
#include <cassert>
#include <unordered_set>
#include <sys/mman.h> // 引入mmap相关的头文件

/**
 * @brief 内存池类，用于高效管理固定类型的对象
 * @tparam T 对象类型
 */
template<typename T>
class MemoryPool {
public:
    MemoryPool(size_t block_size = 1024) 
        : block_size_(block_size), current_block_(nullptr), current_index_(0) {
        allocate_block();
    }

    ~MemoryPool() {
        for (auto block : blocks_) {
            munmap(block, block_size_ * sizeof(T)); // 使用munmap释放大页内存
        }
    }

    // 禁用拷贝构造和赋值操作
    MemoryPool(const MemoryPool&) = delete;
    MemoryPool& operator=(const MemoryPool&) = delete;

    /**
     * @brief 从内存池中分配一个对象
     * @return 指向对象的指针
     */
    T* allocate() {
        if (!freed_objects_.empty()) {
            // 从已释放的对象中获取一个
            T* obj = *freed_objects_.begin();
            freed_objects_.erase(freed_objects_.begin());
            return new (obj) T(); // 在原位置重新构造对象
        }
        
        if (current_index_ >= block_size_) {
            allocate_block();
        }
        return new (current_block_ + current_index_++ * sizeof(T)) T();
    }

    /**
     * @brief 释放对象
     * @param obj 指向要释放的对象的指针
     */
    void deallocate(T* obj) {
        // 这里不实际释放内存，内存池会管理内存
        // 可以在这里实现对象的析构逻辑
        obj->~T();
        
        // 将释放的对象记录下来
        freed_objects_.insert(obj);
    }

private:
    size_t block_size_;                // 每个块的大小
    std::vector<void*> blocks_;        // 存储分配的内存块
    char* current_block_;              // 当前块的指针
    size_t current_index_;             // 当前块中的索引
    std::unordered_set<T*> freed_objects_; // 存储已释放的对象指针

    /**
     * @brief 分配一个新的内存块，使用大页技术
     */
    void allocate_block() {
        // 使用mmap分配大页内存
        current_block_ = static_cast<char*>(mmap(nullptr, block_size_ * sizeof(T), 
            PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
        if (current_block_ == MAP_FAILED) {
            throw std::bad_alloc(); // 如果分配失败，抛出异常
        }
        blocks_.push_back(current_block_);
        current_index_ = 0;
    }
}; 