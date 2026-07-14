cmake_minimum_required(VERSION 3.20)

project(SmartParallel VERSION 0.9.0 LANGUAGES CXX)

find_package(TBB CONFIG REQUIRED)

add_library(smart_parallel
    src/decision.cpp
    src/hardware.cpp
    src/parallel.cpp
    src/thread_pool.cpp
)

add_library(SmartParallel::smart_parallel ALIAS smart_parallel)

target_compile_features(smart_parallel PUBLIC cxx_std_17)

target_include_directories(smart_parallel
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(smart_parallel PUBLIC TBB::tbb)

# NMake places the library and all test executables in the same build
# directory. Copying the oneTBB runtime once here prevents clean CTest runs
# from failing with STATUS_DLL_NOT_FOUND on Windows.
if(WIN32)
    add_custom_command(
        TARGET smart_parallel
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:TBB::tbb>
            $<TARGET_FILE_DIR:smart_parallel>
        COMMENT "Copying oneTBB runtime to the common build output"
    )
endif()

if(MSVC)
    target_compile_options(smart_parallel PRIVATE /W4)
else()
    target_compile_options(smart_parallel PRIVATE -Wall -Wextra -Wpedantic)
endif()

option(SMARTPARALLEL_BUILD_EXAMPLES "Build SmartParallel examples" OFF)

if(SMARTPARALLEL_BUILD_EXAMPLES)
    add_executable(smartparallel_example examples/main.cpp)
    target_link_libraries(smartparallel_example PRIVATE SmartParallel::smart_parallel)
endif()

option(SMARTPARALLEL_BUILD_V1_TESTS "Build SmartParallel V1 development tests" OFF)

if(SMARTPARALLEL_BUILD_V1_TESTS)
    enable_testing()

    add_executable(
        smartparallel_phase1_profiling_semantics
        tests/v1/phase1_profiling_semantics.cpp
    )

    target_link_libraries(
        smartparallel_phase1_profiling_semantics
        PRIVATE SmartParallel::smart_parallel
    )

    add_test(
        NAME smartparallel_phase1_profiling_semantics
        COMMAND smartparallel_phase1_profiling_semantics
    )

    add_executable(
        smartparallel_phase1_sensor_layer
        tests/v1/phase1_sensor_layer.cpp
    )

    target_link_libraries(
        smartparallel_phase1_sensor_layer
        PRIVATE SmartParallel::smart_parallel
    )

    add_test(
        NAME smartparallel_phase1_sensor_layer
        COMMAND smartparallel_phase1_sensor_layer
    )

    add_executable(
        smartparallel_phase1_advanced_observations
        tests/v1/phase1_advanced_observations.cpp
    )

    target_link_libraries(
        smartparallel_phase1_advanced_observations
        PRIVATE SmartParallel::smart_parallel
    )

    add_test(
        NAME smartparallel_phase1_advanced_observations
        COMMAND smartparallel_phase1_advanced_observations
    )

    add_executable(
        smartparallel_phase1_sensor_validation
        tests/v1/phase1_sensor_validation.cpp
    )

    target_link_libraries(
        smartparallel_phase1_sensor_validation
        PRIVATE SmartParallel::smart_parallel
    )

    add_test(
        NAME smartparallel_phase1_sensor_validation
        COMMAND smartparallel_phase1_sensor_validation
    )

    add_executable(
        smartparallel_phase1_profiler_budget
        tests/v1/phase1_profiler_budget.cpp
    )

    target_link_libraries(
        smartparallel_phase1_profiler_budget
        PRIVATE SmartParallel::smart_parallel
    )

    add_test(
        NAME smartparallel_phase1_profiler_budget
        COMMAND smartparallel_phase1_profiler_budget
    )

    add_executable(
        smartparallel_phase2_predictive_model
        tests/v1/phase2_predictive_model.cpp
    )

    target_link_libraries(
        smartparallel_phase2_predictive_model
        PRIVATE SmartParallel::smart_parallel
    )

    add_test(
        NAME smartparallel_phase2_predictive_model
        COMMAND smartparallel_phase2_predictive_model
    )

    add_executable(
        smartparallel_phase2_predictive_shadow
        tests/v1/phase2_predictive_shadow.cpp
    )

    target_link_libraries(
        smartparallel_phase2_predictive_shadow
        PRIVATE SmartParallel::smart_parallel
    )

    add_test(
        NAME smartparallel_phase2_predictive_shadow
        COMMAND smartparallel_phase2_predictive_shadow
    )

    add_executable(
        smartparallel_phase2_experience_persistence
        tests/v1/phase2_experience_persistence.cpp
    )

    target_link_libraries(
        smartparallel_phase2_experience_persistence
        PRIVATE SmartParallel::smart_parallel
    )

    add_test(
        NAME smartparallel_phase2_experience_persistence
        COMMAND smartparallel_phase2_experience_persistence
    )

    add_executable(
        smartparallel_phase2_prediction_calibration
        tests/v1/phase2_prediction_calibration.cpp
    )

    target_link_libraries(
        smartparallel_phase2_prediction_calibration
        PRIVATE SmartParallel::smart_parallel
    )

    add_test(
        NAME smartparallel_phase2_prediction_calibration
        COMMAND smartparallel_phase2_prediction_calibration
    )

    add_executable(
        smartparallel_phase2_machine_runtime_calibration
        tests/v1/phase2_machine_runtime_calibration.cpp
    )

    target_link_libraries(
        smartparallel_phase2_machine_runtime_calibration
        PRIVATE SmartParallel::smart_parallel
    )

    add_test(
        NAME smartparallel_phase2_machine_runtime_calibration
        COMMAND smartparallel_phase2_machine_runtime_calibration
    )
    add_executable(
        smartparallel_phase3_adaptive_candidates
        tests/v1/phase3_adaptive_candidates.cpp
    )

    target_link_libraries(
        smartparallel_phase3_adaptive_candidates
        PRIVATE SmartParallel::smart_parallel
    )

    add_test(
        NAME smartparallel_phase3_adaptive_candidates
        COMMAND smartparallel_phase3_adaptive_candidates
    )

    add_executable(
        smartparallel_phase3_execution_parameters
        tests/v1/phase3_execution_parameters.cpp
    )

    target_link_libraries(
        smartparallel_phase3_execution_parameters
        PRIVATE SmartParallel::smart_parallel
    )

    add_test(
        NAME smartparallel_phase3_execution_parameters
        COMMAND smartparallel_phase3_execution_parameters
    )

    add_executable(
        smartparallel_phase3_worker_aware_calibration
        tests/v1/phase3_worker_aware_calibration.cpp
    )

    target_link_libraries(
        smartparallel_phase3_worker_aware_calibration
        PRIVATE SmartParallel::smart_parallel
    )

    add_test(
        NAME smartparallel_phase3_worker_aware_calibration
        COMMAND smartparallel_phase3_worker_aware_calibration
    )

    add_executable(
        smartparallel_phase4_candidate_ranking
        tests/v1/phase4_candidate_ranking.cpp
    )

    target_link_libraries(
        smartparallel_phase4_candidate_ranking
        PRIVATE SmartParallel::smart_parallel
    )

    add_test(
        NAME smartparallel_phase4_candidate_ranking
        COMMAND smartparallel_phase4_candidate_ranking
    )

    add_executable(
        smartparallel_phase4_ranking_persistence
        tests/v1/phase4_ranking_persistence.cpp
    )

    target_link_libraries(
        smartparallel_phase4_ranking_persistence
        PRIVATE SmartParallel::smart_parallel
    )

    add_test(
        NAME smartparallel_phase4_ranking_persistence
        COMMAND smartparallel_phase4_ranking_persistence
    )

    add_executable(
        smartparallel_phase4_learning_policy
        tests/v1/phase4_learning_policy.cpp
    )

    target_link_libraries(
        smartparallel_phase4_learning_policy
        PRIVATE SmartParallel::smart_parallel
    )

    add_test(
        NAME smartparallel_phase4_learning_policy
        COMMAND smartparallel_phase4_learning_policy
    )

    add_executable(
        smartparallel_phase5_safe_exploration
        tests/v1/phase5_safe_exploration.cpp
    )

    target_link_libraries(
        smartparallel_phase5_safe_exploration
        PRIVATE SmartParallel::smart_parallel
    )

    add_test(
        NAME smartparallel_phase5_safe_exploration
        COMMAND smartparallel_phase5_safe_exploration
    )

    add_executable(
        smartparallel_phase6_workload_family_classifier
        tests/v1/phase6_workload_family_classifier.cpp
    )

    target_link_libraries(
        smartparallel_phase6_workload_family_classifier
        PRIVATE SmartParallel::smart_parallel
    )

    add_test(
        NAME smartparallel_phase6_workload_family_classifier
        COMMAND smartparallel_phase6_workload_family_classifier
    )

    add_executable(
        smartparallel_phase6_family_specific_calibration
        tests/v1/phase6_family_specific_calibration.cpp
    )

    target_link_libraries(
        smartparallel_phase6_family_specific_calibration
        PRIVATE SmartParallel::smart_parallel
    )

    add_test(
        NAME smartparallel_phase6_family_specific_calibration
        COMMAND smartparallel_phase6_family_specific_calibration
    )

    add_executable(
        smartparallel_phase6_residual_correction
        tests/v1/phase6_residual_correction.cpp
    )

    target_link_libraries(
        smartparallel_phase6_residual_correction
        PRIVATE SmartParallel::smart_parallel
    )

    add_test(
        NAME smartparallel_phase6_residual_correction
        COMMAND smartparallel_phase6_residual_correction
    )

    add_executable(
        smartparallel_phase6_similarity_transfer
        tests/v1/phase6_similarity_transfer.cpp
    )

    target_link_libraries(
        smartparallel_phase6_similarity_transfer
        PRIVATE SmartParallel::smart_parallel
    )

    add_test(
        NAME smartparallel_phase6_similarity_transfer
        COMMAND smartparallel_phase6_similarity_transfer
    )

    add_executable(
        smartparallel_phase7_step1_family_aware_residual
        tests/v1/phase7_step1_family_aware_residual.cpp
    )

    target_link_libraries(
        smartparallel_phase7_step1_family_aware_residual
        PRIVATE SmartParallel::smart_parallel
    )

    add_test(
        NAME smartparallel_phase7_step1_family_aware_residual
        COMMAND smartparallel_phase7_step1_family_aware_residual
    )

    add_executable(
        smartparallel_phase7_step2_similarity_normalization
        tests/v1/phase7_step2_similarity_normalization.cpp
    )

    target_link_libraries(
        smartparallel_phase7_step2_similarity_normalization
        PRIVATE SmartParallel::smart_parallel
    )

    add_test(
        NAME smartparallel_phase7_step2_similarity_normalization
        COMMAND smartparallel_phase7_step2_similarity_normalization
    )

    add_executable(
        smartparallel_phase7_step3_historical_overconfidence
        tests/v1/phase7_step3_historical_overconfidence.cpp
    )

    target_link_libraries(
        smartparallel_phase7_step3_historical_overconfidence
        PRIVATE SmartParallel::smart_parallel
    )

    add_test(
        NAME smartparallel_phase7_step3_historical_overconfidence
        COMMAND smartparallel_phase7_step3_historical_overconfidence
    )

    add_executable(
        smartparallel_phase7_step4_confidence_model
        tests/v1/phase7_step4_confidence_model.cpp
    )

    target_link_libraries(
        smartparallel_phase7_step4_confidence_model
        PRIVATE SmartParallel::smart_parallel
    )

    add_test(
        NAME smartparallel_phase7_step4_confidence_model
        COMMAND smartparallel_phase7_step4_confidence_model
    )


    add_executable(
        smartparallel_phase7_step5_enhanced_fingerprint
        tests/v1/phase7_step5_enhanced_fingerprint.cpp
    )
    target_link_libraries(
        smartparallel_phase7_step5_enhanced_fingerprint
        PRIVATE SmartParallel::smart_parallel
    )
    add_test(
        NAME smartparallel_phase7_step5_enhanced_fingerprint
        COMMAND smartparallel_phase7_step5_enhanced_fingerprint
    )

    add_executable(
        smartparallel_phase7_step6_hardware_aware_prediction
        tests/v1/phase7_step6_hardware_aware_prediction.cpp
    )
    target_link_libraries(
        smartparallel_phase7_step6_hardware_aware_prediction
        PRIVATE SmartParallel::smart_parallel
    )
    add_test(
        NAME smartparallel_phase7_step6_hardware_aware_prediction
        COMMAND smartparallel_phase7_step6_hardware_aware_prediction
    )

    add_executable(
        smartparallel_phase8_step1_memory_bandwidth_calibration
        tests/v1/phase8_step1_memory_bandwidth_calibration.cpp
    )
    target_link_libraries(
        smartparallel_phase8_step1_memory_bandwidth_calibration
        PRIVATE SmartParallel::smart_parallel
    )
    add_test(
        NAME smartparallel_phase8_step1_memory_bandwidth_calibration
        COMMAND smartparallel_phase8_step1_memory_bandwidth_calibration
    )

    add_executable(
        smartparallel_phase8_step2_memory_access_calibration
        tests/v1/phase8_step2_memory_access_calibration.cpp
    )
    target_link_libraries(
        smartparallel_phase8_step2_memory_access_calibration
        PRIVATE SmartParallel::smart_parallel
    )
    add_test(
        NAME smartparallel_phase8_step2_memory_access_calibration
        COMMAND smartparallel_phase8_step2_memory_access_calibration
    )

    add_executable(
        smartparallel_phase9_step1_profile_cost_calibration
        tests/v1/phase9_step1_profile_cost_calibration.cpp
    )
    target_link_libraries(
        smartparallel_phase9_step1_profile_cost_calibration
        PRIVATE SmartParallel::smart_parallel
    )
    add_test(
        NAME smartparallel_phase9_step1_profile_cost_calibration
        COMMAND smartparallel_phase9_step1_profile_cost_calibration
    )

endif()

option(SMARTPARALLEL_BUILD_VALIDATION "Build SmartParallel predictive validation tools" OFF)

if(SMARTPARALLEL_BUILD_VALIDATION)
    add_executable(
        smartparallel_prediction_validation
        validation/prediction_validation.cpp
    )

    target_link_libraries(
        smartparallel_prediction_validation
        PRIVATE SmartParallel::smart_parallel
    )

    add_executable(
        smartparallel_prediction_holdout_validation
        validation/holdout_validation.cpp
    )

    target_link_libraries(
        smartparallel_prediction_holdout_validation
        PRIVATE SmartParallel::smart_parallel
    )

    add_executable(
        smartparallel_ranking_evolution_validation
        validation/ranking_evolution_validation.cpp
    )

    target_link_libraries(
        smartparallel_ranking_evolution_validation
        PRIVATE SmartParallel::smart_parallel
    )

    if(WIN32)
        foreach(validation_target
            smartparallel_prediction_validation
            smartparallel_prediction_holdout_validation
            smartparallel_ranking_evolution_validation)
            add_custom_command(
                TARGET ${validation_target}
                POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    $<TARGET_FILE:TBB::tbb>
                    $<TARGET_FILE_DIR:${validation_target}>
                COMMENT "Copying oneTBB runtime beside ${validation_target}"
            )
        endforeach()
    endif()
endif()

if(SMARTPARALLEL_BUILD_V1_TESTS)
    add_executable(
        smartparallel_phase10_learned_runtime_scaling
        tests/v1/phase10_learned_runtime_scaling.cpp
    )
    target_link_libraries(
        smartparallel_phase10_learned_runtime_scaling
        PRIVATE smart_parallel
    )
    add_test(
        NAME smartparallel_phase10_learned_runtime_scaling
        COMMAND smartparallel_phase10_learned_runtime_scaling
    )
endif()
