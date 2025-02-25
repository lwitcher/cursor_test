cmake_minimum_required(VERSION 3.12)
project(LockFreeQueue VERSION 1.0)

# 设置C++标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 性能统计开关
option(ENABLE_PERF_STATS "Enable performance statistics" ON)
if(ENABLE_PERF_STATS)
    add_compile_definitions(
        QUEUE_PERF_STATS=1
        QUEUE_READER_PERF_STATS=1
    )
else()
    add_compile_definitions(
        QUEUE_PERF_STATS=0
        QUEUE_READER_PERF_STATS=0
    )
endif()

# 基础编译选项
add_compile_options(
    -Wall 
    -Wextra 
    -Wpedantic 
    -O3
    -pthread
)

# 架构特定配置
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64")
    message(STATUS "Configuring for x86_64 architecture")
    add_compile_options(-msse4.2)
    add_compile_definitions(TARGET_X86_64)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64")
    message(STATUS "Configuring for ARM64 architecture")
    add_compile_definitions(TARGET_ARM64)
endif()

# 添加可执行文件
add_executable(queue_test main.cpp)

# 直接链接pthread库
target_link_libraries(queue_test PRIVATE pthread)

# 添加头文件目录
target_include_directories(queue_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# 启用测试
enable_testing()
add_test(NAME QueueTest COMMAND queue_test)