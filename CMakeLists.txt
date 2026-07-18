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

option(SMARTPARALLEL_BUILD_OPENCV_TEST1 "Build OpenCV integration Test 1" OFF)
if(SMARTPARALLEL_BUILD_OPENCV_TEST1)
    find_package(OpenCV REQUIRED COMPONENTS core imgproc)
    add_executable(smartparallel_opencv_test1
        benchmarks/opencv/src/test1_threshold.cpp)
    target_include_directories(smartparallel_opencv_test1 PRIVATE
        ${OpenCV_INCLUDE_DIRS})
    target_link_libraries(smartparallel_opencv_test1 PRIVATE
        SmartParallel::smart_parallel
        ${OpenCV_LIBS})

    if(WIN32)
        add_custom_command(TARGET smartparallel_opencv_test1 POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:TBB::tbb>
                $<TARGET_FILE_DIR:smartparallel_opencv_test1>)
    endif()
endif()

option(SMARTPARALLEL_BUILD_OPENCV_TEST2 "Build OpenCV integration Test 2" OFF)
if(SMARTPARALLEL_BUILD_OPENCV_TEST2)
    find_package(OpenCV REQUIRED COMPONENTS core imgproc)
    add_executable(smartparallel_opencv_test2
        benchmarks/opencv/src/test2_convolution.cpp)
    target_include_directories(smartparallel_opencv_test2 PRIVATE
        ${OpenCV_INCLUDE_DIRS})
    target_link_libraries(smartparallel_opencv_test2 PRIVATE
        SmartParallel::smart_parallel
        ${OpenCV_LIBS})

    if(WIN32)
        add_custom_command(TARGET smartparallel_opencv_test2 POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:TBB::tbb>
                $<TARGET_FILE_DIR:smartparallel_opencv_test2>)
    endif()
endif()

option(SMARTPARALLEL_BUILD_OPENCV_TEST3 "Build OpenCV integration Test 3" OFF)
if(SMARTPARALLEL_BUILD_OPENCV_TEST3)
    find_package(OpenCV REQUIRED COMPONENTS core imgproc)
    add_executable(smartparallel_opencv_test3
        benchmarks/opencv/src/test3_sobel.cpp)
    target_include_directories(smartparallel_opencv_test3 PRIVATE
        ${OpenCV_INCLUDE_DIRS})
    target_link_libraries(smartparallel_opencv_test3 PRIVATE
        SmartParallel::smart_parallel
        ${OpenCV_LIBS})

    if(WIN32)
        add_custom_command(TARGET smartparallel_opencv_test3 POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:TBB::tbb>
                $<TARGET_FILE_DIR:smartparallel_opencv_test3>)
    endif()
endif()


option(SMARTPARALLEL_BUILD_OPENCV_STRESS "Build the OpenCV stress benchmark suite" OFF)
if(SMARTPARALLEL_BUILD_OPENCV_STRESS)
    find_package(OpenCV REQUIRED COMPONENTS core imgproc)
    add_executable(smartparallel_opencv_stress
        benchmarks/opencv/src/stress_suite.cpp)
    target_include_directories(smartparallel_opencv_stress PRIVATE
        ${OpenCV_INCLUDE_DIRS})
    target_link_libraries(smartparallel_opencv_stress PRIVATE
        SmartParallel::smart_parallel
        ${OpenCV_LIBS})

    if(WIN32)
        add_custom_command(TARGET smartparallel_opencv_stress POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:TBB::tbb>
                $<TARGET_FILE_DIR:smartparallel_opencv_stress>)
    endif()
endif()

option(SMARTPARALLEL_BUILD_AUTO_PROFILE_TEST
    "Build the parallel_for automatic profiling correctness test" OFF)
if(SMARTPARALLEL_BUILD_AUTO_PROFILE_TEST)
    enable_testing()
    add_executable(smartparallel_parallel_for_auto_profile_test
        tests/v1/parallel_for_auto_profile.cpp)
    target_link_libraries(smartparallel_parallel_for_auto_profile_test
        PRIVATE SmartParallel::smart_parallel)
    add_test(NAME smartparallel_parallel_for_auto_profile
        COMMAND smartparallel_parallel_for_auto_profile_test)
endif()

option(SMARTPARALLEL_BUILD_PARALLEL_FOR_VALIDATION "Build comprehensive parallel_for validation" OFF)
if(SMARTPARALLEL_BUILD_PARALLEL_FOR_VALIDATION)
    enable_testing()
    add_executable(smartparallel_parallel_for_validation tests/v1/parallel_for_validation.cpp)
    target_link_libraries(smartparallel_parallel_for_validation PRIVATE SmartParallel::smart_parallel)
    add_test(NAME smartparallel_parallel_for_validation COMMAND smartparallel_parallel_for_validation)
endif()


option(SMARTPARALLEL_BUILD_PARALLEL_FOR_HARDENING "Build parallel_for hardening tests" OFF)
if(SMARTPARALLEL_BUILD_PARALLEL_FOR_HARDENING)
    enable_testing()
    add_executable(smartparallel_parallel_for_hardening tests/v1/parallel_for_hardening.cpp)
    target_link_libraries(smartparallel_parallel_for_hardening PRIVATE SmartParallel::smart_parallel)
    add_test(NAME smartparallel_parallel_for_hardening COMMAND smartparallel_parallel_for_hardening)
endif()

option(SMARTPARALLEL_BUILD_PARALLEL_FOR_OVERHEAD "Build parallel_for overhead benchmark" OFF)
if(SMARTPARALLEL_BUILD_PARALLEL_FOR_OVERHEAD)
    add_executable(smartparallel_parallel_for_overhead validation/parallel_for_overhead.cpp)
    target_link_libraries(smartparallel_parallel_for_overhead PRIVATE SmartParallel::smart_parallel)
endif()

option(SMARTPARALLEL_BUILD_SCIENTIFIC_TEST1 "Build scientific Test 1: numerical integration" OFF)
if(SMARTPARALLEL_BUILD_SCIENTIFIC_TEST1)
    add_executable(smartparallel_scientific_test1
        benchmarks/scientific/src/test1_numerical_integration.cpp)
    target_link_libraries(smartparallel_scientific_test1 PRIVATE
        SmartParallel::smart_parallel)

    if(WIN32)
        add_custom_command(TARGET smartparallel_scientific_test1 POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:TBB::tbb>
                $<TARGET_FILE_DIR:smartparallel_scientific_test1>)
    endif()
endif()

option(SMARTPARALLEL_BUILD_SCIENTIFIC_TEST2 "Build scientific Test 2: 2D heat diffusion" OFF)
if(SMARTPARALLEL_BUILD_SCIENTIFIC_TEST2)
    add_executable(smartparallel_scientific_test2
        benchmarks/scientific/src/test2_heat_diffusion.cpp)
    target_link_libraries(smartparallel_scientific_test2 PRIVATE
        SmartParallel::smart_parallel)

    if(WIN32)
        add_custom_command(TARGET smartparallel_scientific_test2 POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:TBB::tbb>
                $<TARGET_FILE_DIR:smartparallel_scientific_test2>)
    endif()
endif()

option(SMARTPARALLEL_BUILD_SCIENTIFIC_TEST3 "Build scientific Test 3: irregular particles" OFF)
if(SMARTPARALLEL_BUILD_SCIENTIFIC_TEST3)
    add_executable(smartparallel_scientific_test3
        benchmarks/scientific/src/test3_irregular_particles.cpp)
    target_link_libraries(smartparallel_scientific_test3 PRIVATE
        SmartParallel::smart_parallel)

    if(WIN32)
        add_custom_command(TARGET smartparallel_scientific_test3 POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:TBB::tbb>
                $<TARGET_FILE_DIR:smartparallel_scientific_test3>)
    endif()
endif()

option(SMARTPARALLEL_BUILD_DECISION_QUALITY_AUDIT "Build the all-domain forced-backend decision-quality audit" OFF)
if(SMARTPARALLEL_BUILD_DECISION_QUALITY_AUDIT)
    add_executable(smartparallel_decision_quality_audit
        benchmarks/decision_quality/src/decision_quality_audit.cpp)
    target_link_libraries(smartparallel_decision_quality_audit PRIVATE
        SmartParallel::smart_parallel TBB::tbb)
    if(WIN32)
        add_custom_command(TARGET smartparallel_decision_quality_audit POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:TBB::tbb>
                $<TARGET_FILE_DIR:smartparallel_decision_quality_audit>)
    endif()
endif()
