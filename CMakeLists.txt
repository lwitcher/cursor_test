cmake_minimum_required(VERSION 3.12)
project(LockFreeQueue VERSION 1.0)

# 设置C++标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 添加编译选项
add_compile_options(-Wall -Wextra -Wpedantic)

# 添加可执行文件
add_executable(queue_test main.cpp)

target_link_libraries(queue_test PRIVATE pthread)

# 添加头文件目录
target_include_directories(queue_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# 启用测试
enable_testing()
add_test(NAME QueueTest COMMAND queue_test) 