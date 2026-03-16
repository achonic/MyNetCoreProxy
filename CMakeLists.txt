cmake_minimum_required(VERSION 3.20)

project(MyNetCore VERSION 1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF) # 禁用编译器特有扩展，保证代码的高可移植性

# 忽略未使用的头文件包含警告
add_compile_options(-Wno-unused-include)


# ==========================================
# 1. 核心构建机制：Debug / Release 切换逻辑
# ==========================================
# 如果用户在命令行没有指定编译类型，强制默认使用 Release 模式
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    message(STATUS "No build type selected, default to Release")
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build." FORCE)
    # 给 GUI 工具提供下拉菜单选项
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()


find_package(Threads REQUIRED)
# 依赖



# 定义静态库
add_library(MyNetCoreLib STATIC)

# 关键：链接 Threads 库
target_link_libraries(MyNetCoreLib PUBLIC
    Threads::Threads                   # 需要 pthread
)

target_sources(MyNetCoreLib PRIVATE
    src/net/event_loop.cpp
    src/net/channel.cpp
    src/net/thread_pool.cpp
    src/net/event_loop_thread.cpp
    src/net/acceptor.cpp
    src/net/tcp_connection.cpp
    src/app/http_proxy_handler.cpp
    src/net/buffer.cpp
    src/net/inet_address.cpp
    src/net/server.cpp
)

target_include_directories(MyNetCoreLib PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
)


# ==========================================
# 4. 终极性能压榨：开启 LTO (链接期优化)
# ==========================================
# 检查当前编译器是否支持 IPO/LTO
include(CheckIPOSupported)
check_ipo_supported(RESULT ipo_supported OUTPUT error)
if(ipo_supported)
    # 只要支持，就为你的核心库开启 LTO
    set_target_properties(MyNetCoreLib PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)
    message(STATUS "Ultimate Optimization: IPO/LTO enabled for MyNetCoreLib")
else()
    message(WARNING "IPO/LTO is not supported: ${error}")
endif()


add_executable(echo_server src/demos/echo_server.cpp)
target_link_libraries(echo_server PRIVATE MyNetCoreLib)

add_executable(static_proxy_server src/demos/static_proxy_server.cpp)
target_link_libraries(static_proxy_server PRIVATE MyNetCoreLib)

# 同样为网关程序开启 LTO
if(ipo_supported)
    set_target_properties(echo_server PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)
    set_target_properties(static_proxy_server PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

