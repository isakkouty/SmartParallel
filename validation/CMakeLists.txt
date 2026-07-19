if(SMARTPARALLEL_BUILD_V1_PHASE1)
  add_executable(smartparallel_calibration_dataset prediction_validation.cpp)
  target_link_libraries(
    smartparallel_calibration_dataset
    PRIVATE SmartParallel::smart_parallel
  )
  smartparallel_copy_tbb_runtime(smartparallel_calibration_dataset)

  add_executable(smartparallel_holdout_dataset holdout_validation.cpp)
  target_link_libraries(
    smartparallel_holdout_dataset
    PRIVATE SmartParallel::smart_parallel
  )
  smartparallel_copy_tbb_runtime(smartparallel_holdout_dataset)
endif()

if(SMARTPARALLEL_BUILD_PARALLEL_FOR_OVERHEAD)
  add_executable(smartparallel_parallel_for_overhead parallel_for_overhead.cpp)
  target_link_libraries(
    smartparallel_parallel_for_overhead
    PRIVATE SmartParallel::smart_parallel
  )
  smartparallel_copy_tbb_runtime(smartparallel_parallel_for_overhead)
endif()
