add_library(
  smart_parallel
  decision.cpp
  hardware.cpp
  parallel.cpp
  thread_pool.cpp
)

add_library(SmartParallel::smart_parallel ALIAS smart_parallel)

set_target_properties(
  smart_parallel
  PROPERTIES
    EXPORT_NAME smart_parallel
    VERSION "${PROJECT_VERSION}"
    SOVERSION "${PROJECT_VERSION_MAJOR}"
)

target_compile_features(smart_parallel PUBLIC cxx_std_17)

target_include_directories(
  smart_parallel
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/generated>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_compile_definitions(smart_parallel PUBLIC SMARTPARALLEL_HAS_TBB=${SMARTPARALLEL_HAS_TBB})
if(TBB_FOUND)
  target_link_libraries(smart_parallel PUBLIC TBB::tbb)
endif()

smartparallel_apply_warnings(smart_parallel)
smartparallel_copy_tbb_runtime(smart_parallel)

configure_file(
  "${PROJECT_SOURCE_DIR}/cmake/version.hpp.in"
  "${PROJECT_BINARY_DIR}/generated/smart/version.hpp"
  @ONLY
)
