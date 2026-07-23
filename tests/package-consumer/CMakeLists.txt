cmake_minimum_required(VERSION 3.20)

project(SmartParallelPackageConsumer LANGUAGES CXX)

find_package(SmartParallel 1.4 CONFIG REQUIRED)

add_executable(smartparallel_package_consumer main.cpp)
target_compile_features(smartparallel_package_consumer PRIVATE cxx_std_17)
target_link_libraries(
  smartparallel_package_consumer
  PRIVATE SmartParallel::smart_parallel
)

enable_testing()
add_test(
  NAME smartparallel_package_consumer
  COMMAND smartparallel_package_consumer
)
