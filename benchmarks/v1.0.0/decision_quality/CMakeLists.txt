add_executable(
  smartparallel_decision_quality_audit
  src/decision_quality_audit.cpp
)

target_link_libraries(
  smartparallel_decision_quality_audit
  PRIVATE SmartParallel::smart_parallel TBB::tbb
)

smartparallel_copy_tbb_runtime(smartparallel_decision_quality_audit)
