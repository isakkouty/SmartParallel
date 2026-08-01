if(SMARTPARALLEL_BUILD_V160_SCIENTIFIC_BENCHMARKS)
  add_executable(
    smartparallel_v160_scientific_benchmarks
    src/scientific_foundations_benchmarks.cpp
  )
  target_link_libraries(
    smartparallel_v160_scientific_benchmarks
    PRIVATE SmartParallel::smart_parallel
  )
  smartparallel_apply_warnings(smartparallel_v160_scientific_benchmarks)
  smartparallel_copy_tbb_runtime(smartparallel_v160_scientific_benchmarks)
endif()
