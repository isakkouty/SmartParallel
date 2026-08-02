if(SMARTPARALLEL_BUILD_V180_BENCHMARKS)
  add_executable(
    smartparallel_v180_governed_execution_benchmarks
    src/governed_execution_benchmarks.cpp
  )
  target_link_libraries(
    smartparallel_v180_governed_execution_benchmarks
    PRIVATE SmartParallel::smart_parallel
  )
  smartparallel_apply_warnings(smartparallel_v180_governed_execution_benchmarks)
  smartparallel_copy_tbb_runtime(smartparallel_v180_governed_execution_benchmarks)
endif()
