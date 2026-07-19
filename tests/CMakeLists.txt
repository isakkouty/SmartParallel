if(SMARTPARALLEL_BUILD_V1_PHASE1)
  foreach(test_name
          regret_weighted_ranker
          regret_metrics
          decision_context
          model_persistence
          hybrid_runtime)
    set(target_name "smartparallel_v1_phase1_${test_name}")

    add_executable(${target_name} "v1/phase1_${test_name}.cpp")
    target_link_libraries(${target_name} PRIVATE SmartParallel::smart_parallel)
    smartparallel_copy_tbb_runtime(${target_name})

    add_test(NAME ${target_name} COMMAND ${target_name})
  endforeach()
endif()

if(SMARTPARALLEL_BUILD_AUTO_PROFILE_TEST)
  add_executable(
    smartparallel_parallel_for_auto_profile_test
    v1/parallel_for_auto_profile.cpp
  )
  target_link_libraries(
    smartparallel_parallel_for_auto_profile_test
    PRIVATE SmartParallel::smart_parallel
  )
  smartparallel_copy_tbb_runtime(smartparallel_parallel_for_auto_profile_test)

  add_test(
    NAME smartparallel_parallel_for_auto_profile
    COMMAND smartparallel_parallel_for_auto_profile_test
  )
endif()

if(SMARTPARALLEL_BUILD_PARALLEL_FOR_VALIDATION)
  add_executable(
    smartparallel_parallel_for_validation
    v1/parallel_for_validation.cpp
  )
  target_link_libraries(
    smartparallel_parallel_for_validation
    PRIVATE SmartParallel::smart_parallel
  )
  smartparallel_copy_tbb_runtime(smartparallel_parallel_for_validation)

  add_test(
    NAME smartparallel_parallel_for_validation
    COMMAND smartparallel_parallel_for_validation
  )
endif()

if(SMARTPARALLEL_BUILD_PARALLEL_FOR_HARDENING)
  add_executable(
    smartparallel_parallel_for_hardening
    v1/parallel_for_hardening.cpp
  )
  target_link_libraries(
    smartparallel_parallel_for_hardening
    PRIVATE SmartParallel::smart_parallel
  )
  smartparallel_copy_tbb_runtime(smartparallel_parallel_for_hardening)

  add_test(
    NAME smartparallel_parallel_for_hardening
    COMMAND smartparallel_parallel_for_hardening
  )
endif()
