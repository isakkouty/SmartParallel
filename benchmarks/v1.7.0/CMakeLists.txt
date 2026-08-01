if(SMARTPARALLEL_BUILD_V170_BENCHMARKS)
  add_executable(
    smartparallel_v170_reproducible_runtime_benchmarks
    src/reproducible_runtime_benchmarks.cpp
  )
  target_link_libraries(
    smartparallel_v170_reproducible_runtime_benchmarks
    PRIVATE SmartParallel::smart_parallel
  )
  smartparallel_apply_warnings(smartparallel_v170_reproducible_runtime_benchmarks)
  smartparallel_copy_tbb_runtime(smartparallel_v170_reproducible_runtime_benchmarks)
endif()
