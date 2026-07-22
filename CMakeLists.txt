cmake_minimum_required(VERSION 3.20)

# Preserve the dependency-discovery behavior used by the original Windows
# build scripts. A CMake toolchain must be selected before project(), so use
# VCPKG_ROOT automatically when the caller did not provide another toolchain.
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE AND DEFINED ENV{VCPKG_ROOT})
  set(_smartparallel_vcpkg_toolchain
      "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
  if(EXISTS "${_smartparallel_vcpkg_toolchain}")
    set(CMAKE_TOOLCHAIN_FILE
        "${_smartparallel_vcpkg_toolchain}"
        CACHE FILEPATH "vcpkg toolchain selected from VCPKG_ROOT")
    if(WIN32 AND NOT DEFINED VCPKG_TARGET_TRIPLET)
      set(VCPKG_TARGET_TRIPLET
          "x64-windows"
          CACHE STRING "vcpkg target triplet")
    endif()
  endif()
  unset(_smartparallel_vcpkg_toolchain)
endif()

project(
  SmartParallel
  VERSION 1.1.0
  DESCRIPTION "Adaptive parallel scheduling for modern C++"
  LANGUAGES CXX
)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

option(SMARTPARALLEL_BUILD_EXAMPLES "Build the SmartParallel example" OFF)
option(SMARTPARALLEL_BUILD_VALIDATION "Build all validation and hardening targets" OFF)
option(SMARTPARALLEL_BUILD_BENCHMARKS "Build all benchmark targets" OFF)
option(SMARTPARALLEL_BUILD_ALL "Build examples, validation, and benchmarks" OFF)
option(SMARTPARALLEL_INSTALL "Generate SmartParallel install and package targets" ON)

# Fine-grained compatibility options retained from the original build.
option(SMARTPARALLEL_BUILD_V1_PHASE1
       "Build the current decision-engine validation" OFF)
option(SMARTPARALLEL_BUILD_OPENCV_TEST1
       "Build OpenCV integration Test 1" OFF)
option(SMARTPARALLEL_BUILD_OPENCV_TEST2
       "Build OpenCV integration Test 2" OFF)
option(SMARTPARALLEL_BUILD_OPENCV_TEST3
       "Build OpenCV integration Test 3" OFF)
option(SMARTPARALLEL_BUILD_OPENCV_STRESS
       "Build the OpenCV stress benchmark suite" OFF)
option(SMARTPARALLEL_BUILD_AUTO_PROFILE_TEST
       "Build the parallel_for automatic profiling correctness test" OFF)
option(SMARTPARALLEL_BUILD_PARALLEL_FOR_VALIDATION
       "Build comprehensive parallel_for validation" OFF)
option(SMARTPARALLEL_BUILD_PARALLEL_FOR_HARDENING
       "Build parallel_for hardening tests" OFF)
option(SMARTPARALLEL_BUILD_PARALLEL_FOR_OVERHEAD
       "Build parallel_for overhead benchmark" OFF)
option(SMARTPARALLEL_BUILD_SCIENTIFIC_TEST1
       "Build scientific Test 1: numerical integration" OFF)
option(SMARTPARALLEL_BUILD_SCIENTIFIC_TEST2
       "Build scientific Test 2: 2D heat diffusion" OFF)
option(SMARTPARALLEL_BUILD_SCIENTIFIC_TEST3
       "Build scientific Test 3: irregular particles" OFF)
option(SMARTPARALLEL_BUILD_DECISION_QUALITY_AUDIT
       "Build the all-domain forced-backend decision-quality audit" OFF)
option(SMARTPARALLEL_BUILD_V110_NESTED_BENCHMARKS
       "Build the v1.1.0 nested execution benchmark suite" OFF)
option(SMARTPARALLEL_BUILD_REAL_WORLD_BENCHMARKS
       "Build all v1.1 real-world integration benchmarks" OFF)
option(SMARTPARALLEL_BUILD_OPENCV_BENCHMARK
       "Build the OpenCV image-pipeline integration benchmark" OFF)
option(SMARTPARALLEL_BUILD_LZ4_BENCHMARK
       "Build the LZ4 batch-compression integration benchmark" OFF)
option(SMARTPARALLEL_BUILD_BVH_BENCHMARK
       "Build the BVH construction integration benchmark" OFF)
option(SMARTPARALLEL_BUILD_PARTICLE_BENCHMARK
       "Build the particle-simulation integration benchmark" OFF)
option(SMARTPARALLEL_REQUIRE_REAL_WORLD_DEPENDENCIES
       "Fail configuration when a requested real-world dependency is unavailable" OFF)

include(SmartParallelOptions)
include(SmartParallelHelpers)

option(SMARTPARALLEL_ENABLE_TBB "Enable the oneTBB execution backend" ON)
option(SMARTPARALLEL_REQUIRE_TBB
       "Fail configuration when oneTBB backend validation was requested but TBB is unavailable"
       OFF)
if(SMARTPARALLEL_ENABLE_TBB)
  find_package(TBB CONFIG QUIET)
endif()

if(TBB_FOUND)
  set(SMARTPARALLEL_HAS_TBB 1)
else()
  set(SMARTPARALLEL_HAS_TBB 0)
  if(SMARTPARALLEL_REQUIRE_TBB)
    message(FATAL_ERROR
            "SMARTPARALLEL_REQUIRE_TBB=ON but oneTBB was not found")
  endif()
  if(SMARTPARALLEL_ENABLE_TBB)
    message(WARNING "oneTBB was not found; building the ThreadPool/StaticThread backends only")
  endif()
endif()

add_subdirectory(src)

if(SMARTPARALLEL_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()

if(SMARTPARALLEL_BUILD_ANY_VALIDATION)
  enable_testing()
  add_subdirectory(tests)
  add_subdirectory(validation)
endif()

if(SMARTPARALLEL_BUILD_ANY_BENCHMARK)
  add_subdirectory(benchmarks)
endif()

if(SMARTPARALLEL_INSTALL)
  install(
    DIRECTORY include/
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    COMPONENT Development
  )

  install(
    FILES "${CMAKE_CURRENT_BINARY_DIR}/generated/smart/version.hpp"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/smart"
    COMPONENT Development
  )

  install(
    TARGETS smart_parallel
    EXPORT SmartParallelTargets
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
            COMPONENT Runtime
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
            COMPONENT Runtime
            NAMELINK_COMPONENT Development
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
            COMPONENT Development
    INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
  )

  configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/SmartParallelConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/SmartParallelConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/SmartParallel"
  )

  write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/SmartParallelConfigVersion.cmake"
    VERSION "${PROJECT_VERSION}"
    COMPATIBILITY SameMajorVersion
  )

  install(
    EXPORT SmartParallelTargets
    FILE SmartParallelTargets.cmake
    NAMESPACE SmartParallel::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/SmartParallel"
    COMPONENT Development
  )

  install(
    FILES
      "${CMAKE_CURRENT_BINARY_DIR}/SmartParallelConfig.cmake"
      "${CMAKE_CURRENT_BINARY_DIR}/SmartParallelConfigVersion.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/SmartParallel"
    COMPONENT Development
  )
endif()
