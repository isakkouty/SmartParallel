set(_smartparallel_real_world_requested OFF)
if(SMARTPARALLEL_BUILD_OPENCV_BENCHMARK
   OR SMARTPARALLEL_BUILD_LZ4_BENCHMARK
   OR SMARTPARALLEL_BUILD_BVH_BENCHMARK
   OR SMARTPARALLEL_BUILD_PARTICLE_BENCHMARK)
  set(_smartparallel_real_world_requested ON)
endif()

if(NOT _smartparallel_real_world_requested)
  return()
endif()

add_library(smartparallel_real_world_common INTERFACE)
target_include_directories(
  smartparallel_real_world_common
  INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_link_libraries(
  smartparallel_real_world_common
  INTERFACE SmartParallel::smart_parallel
)
target_compile_features(smartparallel_real_world_common INTERFACE cxx_std_17)
if(WIN32)
  target_link_libraries(smartparallel_real_world_common INTERFACE psapi)
endif()

set(SMARTPARALLEL_BENCHMARK_COMMIT "" CACHE STRING
    "Source revision recorded in real-world benchmark metadata")
if(NOT SMARTPARALLEL_BENCHMARK_COMMIT)
  find_package(Git QUIET)
  if(Git_FOUND)
    execute_process(
      COMMAND "${GIT_EXECUTABLE}" rev-parse --short=12 HEAD
      WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
      RESULT_VARIABLE _smartparallel_git_result
      OUTPUT_VARIABLE _smartparallel_git_revision
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET)
    if(_smartparallel_git_result EQUAL 0 AND _smartparallel_git_revision)
      set(SMARTPARALLEL_BENCHMARK_COMMIT "${_smartparallel_git_revision}")
    endif()
  endif()
endif()
if(NOT SMARTPARALLEL_BENCHMARK_COMMIT)
  set(SMARTPARALLEL_BENCHMARK_COMMIT "source_archive")
endif()
target_compile_definitions(
  smartparallel_real_world_common
  INTERFACE SMARTPARALLEL_BENCHMARK_COMMIT="${SMARTPARALLEL_BENCHMARK_COMMIT}"
)

function(smartparallel_add_real_world_benchmark target source)
  add_executable(${target} "src/${source}")
  target_link_libraries(${target} PRIVATE smartparallel_real_world_common)
  smartparallel_apply_warnings(${target})
  smartparallel_copy_tbb_runtime(${target})
endfunction()

if(SMARTPARALLEL_BUILD_OPENCV_BENCHMARK)
  find_package(OpenCV CONFIG QUIET COMPONENTS core imgproc)
  if(NOT OpenCV_FOUND)
    # System OpenCV installations frequently provide module mode rather than a
    # config package. Keep the vcpkg path first, then try the normal CMake finder.
    find_package(OpenCV QUIET COMPONENTS core imgproc)
  endif()
  if(OpenCV_FOUND)
    smartparallel_add_real_world_benchmark(
      smartparallel_real_world_opencv
      opencv_image_pipeline.cpp
    )
    target_include_directories(
      smartparallel_real_world_opencv PRIVATE ${OpenCV_INCLUDE_DIRS}
    )
    target_link_libraries(
      smartparallel_real_world_opencv PRIVATE ${OpenCV_LIBS}
    )
    target_compile_definitions(
      smartparallel_real_world_opencv
      PRIVATE SMARTPARALLEL_REAL_WORLD_OPENCV_VERSION="${OpenCV_VERSION}"
    )
  elseif(SMARTPARALLEL_REQUIRE_REAL_WORLD_DEPENDENCIES)
    message(FATAL_ERROR
      "SMARTPARALLEL_BUILD_OPENCV_BENCHMARK=ON but OpenCV core/imgproc was not found")
  else()
    message(WARNING
      "OpenCV real-world benchmark disabled because OpenCV core/imgproc was not found")
  endif()
endif()

if(SMARTPARALLEL_BUILD_LZ4_BENCHMARK)
  set(_smartparallel_lz4_target "")
  find_package(lz4 CONFIG QUIET)
  foreach(_candidate lz4::lz4 LZ4::lz4 LZ4::LZ4 lz4::lz4_static lz4::lz4_shared)
    if(TARGET ${_candidate} AND NOT _smartparallel_lz4_target)
      set(_smartparallel_lz4_target ${_candidate})
    endif()
  endforeach()
  if(NOT _smartparallel_lz4_target)
    find_path(SMARTPARALLEL_LZ4_INCLUDE_DIR lz4.h)
    find_library(SMARTPARALLEL_LZ4_LIBRARY NAMES lz4 liblz4)
    if(SMARTPARALLEL_LZ4_INCLUDE_DIR AND SMARTPARALLEL_LZ4_LIBRARY)
      add_library(smartparallel_lz4_external UNKNOWN IMPORTED)
      set_target_properties(smartparallel_lz4_external PROPERTIES
        IMPORTED_LOCATION "${SMARTPARALLEL_LZ4_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${SMARTPARALLEL_LZ4_INCLUDE_DIR}")
      set(_smartparallel_lz4_target smartparallel_lz4_external)
    endif()
  endif()
  if(_smartparallel_lz4_target)
    smartparallel_add_real_world_benchmark(
      smartparallel_real_world_lz4
      lz4_batch_compression.cpp
    )
    target_link_libraries(
      smartparallel_real_world_lz4 PRIVATE ${_smartparallel_lz4_target}
    )
  elseif(SMARTPARALLEL_REQUIRE_REAL_WORLD_DEPENDENCIES)
    message(FATAL_ERROR
      "SMARTPARALLEL_BUILD_LZ4_BENCHMARK=ON but LZ4 was not found")
  else()
    message(WARNING
      "LZ4 real-world benchmark disabled because LZ4 was not found")
  endif()
endif()

if(SMARTPARALLEL_BUILD_BVH_BENCHMARK)
  smartparallel_add_real_world_benchmark(
    smartparallel_real_world_bvh
    bvh_construction.cpp
  )
endif()

if(SMARTPARALLEL_BUILD_PARTICLE_BENCHMARK)
  smartparallel_add_real_world_benchmark(
    smartparallel_real_world_particles
    particle_simulation.cpp
  )
endif()

if(SMARTPARALLEL_BUILD_ANY_VALIDATION)
  if(TARGET smartparallel_real_world_opencv)
    add_test(
      NAME smartparallel_real_world_opencv_smoke
      COMMAND smartparallel_real_world_opencv
        --preset tiny --mode sequential,smart_auto --backend thread_pool
        --repetitions 1 --warmups 0 --workers 4
        --output-dir "${PROJECT_BINARY_DIR}/real_world_test_output/opencv"
    )
  endif()
  if(TARGET smartparallel_real_world_lz4)
    add_test(
      NAME smartparallel_real_world_lz4_smoke
      COMMAND smartparallel_real_world_lz4
        --preset tiny_compressible --mode sequential,smart_auto --backend thread_pool
        --repetitions 1 --warmups 0 --workers 4
        --output-dir "${PROJECT_BINARY_DIR}/real_world_test_output/lz4"
    )
  endif()
  if(TARGET smartparallel_real_world_bvh)
    add_test(
      NAME smartparallel_real_world_bvh_smoke
      COMMAND smartparallel_real_world_bvh
        --preset small_uniform --mode sequential,smart_auto --backend thread_pool
        --repetitions 1 --warmups 0 --workers 4
        --output-dir "${PROJECT_BINARY_DIR}/real_world_test_output/bvh"
    )
  endif()
  if(TARGET smartparallel_real_world_particles)
    add_test(
      NAME smartparallel_real_world_particles_smoke
      COMMAND smartparallel_real_world_particles
        --preset tiny --mode sequential,smart_auto --backend thread_pool
        --repetitions 1 --warmups 0 --workers 4
        --output-dir "${PROJECT_BINARY_DIR}/real_world_test_output/particles"
    )
  endif()
endif()
