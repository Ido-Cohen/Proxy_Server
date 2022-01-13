cmake_minimum_required(VERSION 3.16)
project(Proxy_Server C)

set(CMAKE_C_STANDARD 99)
set(GCC_COVERAGE_COMPILE_FLAGS "-pthread")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${GCC_COVERAGE_COMPILE_FLAGS}" )

add_executable(Proxy_Server proxyServer.c threadpool.c threadpool.h)
