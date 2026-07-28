if(SMARTPARALLEL_BUILD_V150_VISION_BENCHMARKS)
  if(NOT TARGET smart_parallel_vision)
    message(FATAL_ERROR
            "SMARTPARALLEL_BUILD_V150_VISION_BENCHMARKS requires SMARTPARALLEL_BUILD_VISION=ON")
  endif()
  if(NOT SMARTPARALLEL_VISION_HAS_OPENCV)
    message(FATAL_ERROR
            "v1.5 route benchmarks require SMARTPARALLEL_ENABLE_OPENCV_PROVIDER=ON and OpenCV")
  endif()
  add_executable(
    smartparallel_v150_adaptive_routes_benchmarks
    src/adaptive_routes_benchmarks.cpp
  )
  target_link_libraries(
    smartparallel_v150_adaptive_routes_benchmarks
    PRIVATE SmartParallel::vision
  )
  if(TARGET opencv_core AND TARGET opencv_imgproc)
    target_link_libraries(
      smartparallel_v150_adaptive_routes_benchmarks
      PRIVATE opencv_core opencv_imgproc
    )
  else()
    target_include_directories(
      smartparallel_v150_adaptive_routes_benchmarks
      PRIVATE ${OpenCV_INCLUDE_DIRS}
    )
    target_link_libraries(
      smartparallel_v150_adaptive_routes_benchmarks
      PRIVATE ${OpenCV_LIBS}
    )
  endif()
  smartparallel_copy_tbb_runtime(smartparallel_v150_adaptive_routes_benchmarks)
endif()
