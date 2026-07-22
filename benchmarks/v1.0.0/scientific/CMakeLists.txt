function(smartparallel_add_scientific_benchmark target_name source_file)
  add_executable(${target_name} "src/${source_file}")
  target_link_libraries(${target_name} PRIVATE SmartParallel::smart_parallel)
  smartparallel_copy_tbb_runtime(${target_name})
endfunction()

if(SMARTPARALLEL_BUILD_SCIENTIFIC_TEST1)
  smartparallel_add_scientific_benchmark(
    smartparallel_scientific_test1
    test1_numerical_integration.cpp
  )
endif()

if(SMARTPARALLEL_BUILD_SCIENTIFIC_TEST2)
  smartparallel_add_scientific_benchmark(
    smartparallel_scientific_test2
    test2_heat_diffusion.cpp
  )
endif()

if(SMARTPARALLEL_BUILD_SCIENTIFIC_TEST3)
  smartparallel_add_scientific_benchmark(
    smartparallel_scientific_test3
    test3_irregular_particles.cpp
  )
endif()
