cmake_minimum_required(VERSION 3.20)
project(SmartParallel VERSION 1.0.0 LANGUAGES CXX)

find_package(TBB CONFIG REQUIRED)

add_library(smart_parallel
    src/decision.cpp
    src/hardware.cpp
    src/parallel.cpp
    src/thread_pool.cpp)
add_library(SmartParallel::smart_parallel ALIAS smart_parallel)
target_compile_features(smart_parallel PUBLIC cxx_std_17)
target_include_directories(smart_parallel PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_link_libraries(smart_parallel PUBLIC TBB::tbb)

if(MSVC)
    target_compile_options(smart_parallel PRIVATE /W4)
else()
    target_compile_options(smart_parallel PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(WIN32)
    add_custom_command(TARGET smart_parallel POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:TBB::tbb> $<TARGET_FILE_DIR:smart_parallel>
        COMMENT "Copying oneTBB runtime to the build output")
endif()

option(SMARTPARALLEL_BUILD_EXAMPLES "Build example" OFF)
if(SMARTPARALLEL_BUILD_EXAMPLES)
    add_executable(smartparallel_example examples/main.cpp)
    target_link_libraries(smartparallel_example PRIVATE SmartParallel::smart_parallel)
endif()

option(SMARTPARALLEL_BUILD_V1_PHASE1 "Build the current decision-engine validation" OFF)
if(SMARTPARALLEL_BUILD_V1_PHASE1)
    enable_testing()
    foreach(test_name regret_weighted_ranker regret_metrics decision_context model_persistence hybrid_runtime)
        add_executable(smartparallel_v1_phase1_${test_name}
            tests/v1/phase1_${test_name}.cpp)
        target_link_libraries(smartparallel_v1_phase1_${test_name}
            PRIVATE SmartParallel::smart_parallel)
        add_test(NAME smartparallel_v1_phase1_${test_name}
            COMMAND smartparallel_v1_phase1_${test_name})
    endforeach()

    add_executable(smartparallel_calibration_dataset validation/prediction_validation.cpp)
    target_link_libraries(smartparallel_calibration_dataset PRIVATE SmartParallel::smart_parallel)
    add_executable(smartparallel_holdout_dataset validation/holdout_validation.cpp)
    target_link_libraries(smartparallel_holdout_dataset PRIVATE SmartParallel::smart_parallel)

    if(WIN32)
        foreach(target smartparallel_calibration_dataset smartparallel_holdout_dataset)
            add_custom_command(TARGET ${target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    $<TARGET_FILE:TBB::tbb> $<TARGET_FILE_DIR:${target}>)
        endforeach()
    endif()
endif()
