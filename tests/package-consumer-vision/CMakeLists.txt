cmake_minimum_required(VERSION 3.20)
project(SmartParallelVisionPackageConsumer LANGUAGES CXX)

find_package(SmartParallel CONFIG REQUIRED)
find_package(SmartParallelVision CONFIG REQUIRED)

add_executable(smartparallel_vision_package_consumer main.cpp)
target_compile_features(smartparallel_vision_package_consumer PRIVATE cxx_std_17)
target_link_libraries(smartparallel_vision_package_consumer PRIVATE SmartParallel::vision)


enable_testing()
add_test(
  NAME smartparallel_vision_package_consumer
  COMMAND smartparallel_vision_package_consumer
)
