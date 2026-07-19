find_package(OpenCV REQUIRED COMPONENTS core imgproc)

function(smartparallel_add_opencv_benchmark target_name source_file)
  add_executable(${target_name} "src/${source_file}")
  target_include_directories(${target_name} PRIVATE ${OpenCV_INCLUDE_DIRS})
  target_link_libraries(
    ${target_name}
    PRIVATE SmartParallel::smart_parallel ${OpenCV_LIBS}
  )
  smartparallel_copy_tbb_runtime(${target_name})
endfunction()

if(SMARTPARALLEL_BUILD_OPENCV_TEST1)
  smartparallel_add_opencv_benchmark(
    smartparallel_opencv_test1
    test1_threshold.cpp
  )
endif()

if(SMARTPARALLEL_BUILD_OPENCV_TEST2)
  smartparallel_add_opencv_benchmark(
    smartparallel_opencv_test2
    test2_convolution.cpp
  )
endif()

if(SMARTPARALLEL_BUILD_OPENCV_TEST3)
  smartparallel_add_opencv_benchmark(
    smartparallel_opencv_test3
    test3_sobel.cpp
  )
endif()

if(SMARTPARALLEL_BUILD_OPENCV_STRESS)
  smartparallel_add_opencv_benchmark(
    smartparallel_opencv_stress
    stress_suite.cpp
  )
endif()
