if(SMARTPARALLEL_BUILD_V110_NESTED_BENCHMARKS)
  add_executable(
    smartparallel_v110_nested_benchmarks
    src/nested_execution_benchmarks.cpp
  )

  target_link_libraries(
    smartparallel_v110_nested_benchmarks
    PRIVATE SmartParallel::smart_parallel
  )

  smartparallel_copy_tbb_runtime(smartparallel_v110_nested_benchmarks)
endif()
