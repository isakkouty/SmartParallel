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

  add_executable(
    smartparallel_nested_session_frontier_validation
    v1/nested_session_frontier.cpp
  )
  target_link_libraries(
    smartparallel_nested_session_frontier_validation
    PRIVATE SmartParallel::smart_parallel
  )
  smartparallel_copy_tbb_runtime(smartparallel_nested_session_frontier_validation)

  add_test(
    NAME smartparallel_nested_session_frontier_validation
    COMMAND smartparallel_nested_session_frontier_validation
  )

  add_executable(
    smartparallel_nested_session_hardening
    v1/nested_session_hardening.cpp
  )
  target_link_libraries(
    smartparallel_nested_session_hardening
    PRIVATE SmartParallel::smart_parallel
  )
  smartparallel_copy_tbb_runtime(smartparallel_nested_session_hardening)

  add_test(
    NAME smartparallel_nested_session_hardening
    COMMAND smartparallel_nested_session_hardening
  )

  add_executable(
    smartparallel_nested_irregular_validation
    v1/nested_irregular_validation.cpp
  )
  target_link_libraries(
    smartparallel_nested_irregular_validation
    PRIVATE SmartParallel::smart_parallel
  )
  smartparallel_copy_tbb_runtime(smartparallel_nested_irregular_validation)

  add_test(
    NAME smartparallel_nested_irregular_validation
    COMMAND smartparallel_nested_irregular_validation
  )
  set_tests_properties(
    smartparallel_nested_irregular_validation
    PROPERTIES TIMEOUT 30
  )

  add_executable(
    smartparallel_nested_production_stress
    v1/nested_production_stress.cpp
  )
  target_link_libraries(
    smartparallel_nested_production_stress
    PRIVATE SmartParallel::smart_parallel
  )
  smartparallel_copy_tbb_runtime(smartparallel_nested_production_stress)

  add_test(
    NAME smartparallel_nested_production_stress
    COMMAND smartparallel_nested_production_stress
  )
  set_tests_properties(
    smartparallel_nested_production_stress
    PROPERTIES TIMEOUT 60
  )
endif()
