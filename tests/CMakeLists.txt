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
    smartparallel_hardware_characteristics_portability
    v1/hardware_characteristics_portability.cpp
  )
  target_link_libraries(
    smartparallel_hardware_characteristics_portability
    PRIVATE SmartParallel::smart_parallel
  )
  smartparallel_copy_tbb_runtime(smartparallel_hardware_characteristics_portability)

  add_test(
    NAME smartparallel_hardware_characteristics_portability
    COMMAND smartparallel_hardware_characteristics_portability
  )

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

  add_executable(
    smartparallel_v11_preintegration_release_gates
    v1/preintegration_release_gates.cpp
  )
  target_link_libraries(
    smartparallel_v11_preintegration_release_gates
    PRIVATE SmartParallel::smart_parallel
  )
  smartparallel_copy_tbb_runtime(smartparallel_v11_preintegration_release_gates)

  add_test(
    NAME smartparallel_v11_preintegration_release_gates
    COMMAND smartparallel_v11_preintegration_release_gates
  )
  set_tests_properties(
    smartparallel_v11_preintegration_release_gates
    PROPERTIES TIMEOUT 90
  )

  add_executable(
    smartparallel_real_world_optimization_hardening
    v1/real_world_optimization_hardening.cpp
  )
  target_link_libraries(
    smartparallel_real_world_optimization_hardening
    PRIVATE SmartParallel::smart_parallel
  )
  smartparallel_copy_tbb_runtime(smartparallel_real_world_optimization_hardening)

  add_test(
    NAME smartparallel_real_world_optimization_hardening
    COMMAND smartparallel_real_world_optimization_hardening
  )
  set_tests_properties(
    smartparallel_real_world_optimization_hardening
    PROPERTIES TIMEOUT 30
  )

  add_executable(
    smartparallel_nested_shutdown_stress
    v1/nested_shutdown_stress.cpp
  )
  target_link_libraries(
    smartparallel_nested_shutdown_stress
    PRIVATE SmartParallel::smart_parallel
  )
  smartparallel_copy_tbb_runtime(smartparallel_nested_shutdown_stress)

  add_test(
    NAME smartparallel_nested_shutdown_stress
    COMMAND smartparallel_nested_shutdown_stress
  )
  set_tests_properties(
    smartparallel_nested_shutdown_stress
    PROPERTIES TIMEOUT 30
  )
endif()

if(SMARTPARALLEL_BUILD_V140_ALGORITHM_VALIDATION)
  add_executable(
    smartparallel_v140_parallel_algorithms_validation
    v1/parallel_algorithms_validation.cpp
  )
  target_link_libraries(
    smartparallel_v140_parallel_algorithms_validation
    PRIVATE SmartParallel::smart_parallel
  )
  smartparallel_copy_tbb_runtime(smartparallel_v140_parallel_algorithms_validation)

  add_test(
    NAME smartparallel_v140_parallel_algorithms_validation
    COMMAND smartparallel_v140_parallel_algorithms_validation
  )
  set_tests_properties(
    smartparallel_v140_parallel_algorithms_validation
    PROPERTIES TIMEOUT 120
  )
endif()

if(SMARTPARALLEL_BUILD_V150_VISION_VALIDATION)
  if(NOT TARGET smart_parallel_vision)
    message(FATAL_ERROR
            "SMARTPARALLEL_BUILD_V150_VISION_VALIDATION requires SMARTPARALLEL_BUILD_VISION=ON")
  endif()
  add_executable(
    smartparallel_v150_vision_adaptive_routes_validation
    v1/vision_adaptive_routes_validation.cpp
    v1/threshold_vectorization_probe.cpp
  )
  if(MSVC)
    set_source_files_properties(
      v1/threshold_vectorization_probe.cpp
      PROPERTIES COMPILE_OPTIONS "/Qvec-report:2"
    )
  endif()
  target_link_libraries(
    smartparallel_v150_vision_adaptive_routes_validation
    PRIVATE SmartParallel::vision
  )
  smartparallel_copy_tbb_runtime(smartparallel_v150_vision_adaptive_routes_validation)
  add_test(
    NAME smartparallel_v150_vision_adaptive_routes_validation
    COMMAND smartparallel_v150_vision_adaptive_routes_validation
  )
  set_tests_properties(
    smartparallel_v150_vision_adaptive_routes_validation
    PROPERTIES TIMEOUT 120
  )
endif()

if(SMARTPARALLEL_BUILD_V160_SCIENTIFIC_VALIDATION)
  add_executable(
    smartparallel_v160_scientific_foundations_validation
    v1.6/scientific_foundations.cpp
  )
  target_link_libraries(
    smartparallel_v160_scientific_foundations_validation
    PRIVATE SmartParallel::smart_parallel
  )
  smartparallel_apply_warnings(smartparallel_v160_scientific_foundations_validation)
  smartparallel_copy_tbb_runtime(smartparallel_v160_scientific_foundations_validation)
  add_test(
    NAME smartparallel_v160_scientific_foundations_validation
    COMMAND smartparallel_v160_scientific_foundations_validation
  )
  set_tests_properties(
    smartparallel_v160_scientific_foundations_validation
    PROPERTIES TIMEOUT 120
  )

  if(TARGET smart_parallel_vision)
    add_executable(
      smartparallel_v160_vision_view_adapter_validation
      v1.6/vision_view_adapter.cpp
    )
    target_link_libraries(
      smartparallel_v160_vision_view_adapter_validation
      PRIVATE SmartParallel::vision
    )
    smartparallel_apply_warnings(smartparallel_v160_vision_view_adapter_validation)
    add_test(
      NAME smartparallel_v160_vision_view_adapter_validation
      COMMAND smartparallel_v160_vision_view_adapter_validation
    )
  endif()
endif()

if(SMARTPARALLEL_BUILD_V170_REPRODUCIBLE_RUNTIME_VALIDATION)
  add_executable(
    smartparallel_v170_reproducible_runtime_validation
    v1.7/reproducible_runtime.cpp
  )
  target_link_libraries(
    smartparallel_v170_reproducible_runtime_validation
    PRIVATE SmartParallel::smart_parallel
  )
  smartparallel_apply_warnings(smartparallel_v170_reproducible_runtime_validation)
  target_compile_definitions(
    smartparallel_v170_reproducible_runtime_validation
    PRIVATE SMARTPARALLEL_V170_MALFORMED_PROFILE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/v1.7/malformed_profiles"
  )
  smartparallel_copy_tbb_runtime(smartparallel_v170_reproducible_runtime_validation)
  add_test(
    NAME smartparallel_v170_reproducible_runtime_validation
    COMMAND smartparallel_v170_reproducible_runtime_validation
  )
  set_tests_properties(
    smartparallel_v170_reproducible_runtime_validation
    PROPERTIES TIMEOUT 120
  )

  add_executable(
    smartparallel_v170_scientific_profile_validation
    v1.7/scientific_profiles.cpp
  )
  target_link_libraries(
    smartparallel_v170_scientific_profile_validation
    PRIVATE SmartParallel::smart_parallel
  )
  smartparallel_apply_warnings(smartparallel_v170_scientific_profile_validation)
  smartparallel_copy_tbb_runtime(smartparallel_v170_scientific_profile_validation)
  add_test(
    NAME smartparallel_v170_scientific_profile_validation
    COMMAND smartparallel_v170_scientific_profile_validation
  )
  set_tests_properties(
    smartparallel_v170_scientific_profile_validation
    PROPERTIES TIMEOUT 120
  )

  if(TARGET smart_parallel_vision)
    add_executable(
      smartparallel_v170_vision_runtime_validation
      v1.7/vision_runtime.cpp
    )
    target_link_libraries(
      smartparallel_v170_vision_runtime_validation
      PRIVATE SmartParallel::vision
    )
    smartparallel_apply_warnings(smartparallel_v170_vision_runtime_validation)
    smartparallel_copy_tbb_runtime(smartparallel_v170_vision_runtime_validation)
    add_test(
      NAME smartparallel_v170_vision_runtime_validation
      COMMAND smartparallel_v170_vision_runtime_validation
    )
    set_tests_properties(smartparallel_v170_vision_runtime_validation PROPERTIES TIMEOUT 120)
  endif()

  if(TARGET smartparallel_calibrate AND TARGET smartparallel_profile AND TARGET smartparallel_replay)
    add_test(
      NAME smartparallel_v170_cross_process_replay
      COMMAND "${CMAKE_COMMAND}"
        "-DCALIBRATE=$<TARGET_FILE:smartparallel_calibrate>"
        "-DPROFILE=$<TARGET_FILE:smartparallel_profile>"
        "-DREPLAY=$<TARGET_FILE:smartparallel_replay>"
        "-DROOT=${CMAKE_CURRENT_BINARY_DIR}/v170_cross_process"
        -P "${PROJECT_SOURCE_DIR}/tests/v1.7/cross_process.cmake"
    )
    set_tests_properties(smartparallel_v170_cross_process_replay PROPERTIES TIMEOUT 120)
  endif()
endif()
