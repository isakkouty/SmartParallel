cmake_minimum_required(VERSION 3.20)

project(SmartParallel VERSION 0.9.0 LANGUAGES CXX)

find_package(TBB CONFIG REQUIRED)

add_library(smart_parallel
    src/decision.cpp
    src/hardware.cpp
    src/parallel.cpp
    src/thread_pool.cpp
)

add_library(SmartParallel::smart_parallel ALIAS smart_parallel)

target_compile_features(smart_parallel PUBLIC cxx_std_17)

target_include_directories(smart_parallel
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(smart_parallel PUBLIC TBB::tbb)

if(MSVC)
    target_compile_options(smart_parallel PRIVATE /W4)
else()
    target_compile_options(smart_parallel PRIVATE -Wall -Wextra -Wpedantic)
endif()

option(SMARTPARALLEL_BUILD_EXAMPLES "Build SmartParallel examples" OFF)

if(SMARTPARALLEL_BUILD_EXAMPLES)
    add_executable(smartparallel_example examples/main.cpp)
    target_link_libraries(smartparallel_example PRIVATE SmartParallel::smart_parallel)
endif()
