if(SMARTPARALLEL_BUILD_V140_ALGORITHM_BENCHMARKS)
  add_executable(
    smartparallel_v140_algorithm_benchmarks
    src/parallel_algorithm_benchmarks.cpp
  )
  target_link_libraries(
    smartparallel_v140_algorithm_benchmarks
    PRIVATE SmartParallel::smart_parallel
  )
  smartparallel_copy_tbb_runtime(smartparallel_v140_algorithm_benchmarks)
endif()
