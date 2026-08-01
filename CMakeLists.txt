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
  VERSION 1.6.0
  DESCRIPTION "Adaptive parallel execution for modern C++"
  LANGUAGES CXX
)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

find_package(Threads REQUIRED)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

option(SMARTPARALLEL_BUILD_EXAMPLES "Build the SmartParallel example" OFF)
option(SMARTPARALLEL_BUILD_VALIDATION "Build all validation and hardening targets" OFF)
option(SMARTPARALLEL_BUILD_BENCHMARKS "Build all benchmark targets" OFF)
option(SMARTPARALLEL_BUILD_ALL "Build examples, validation, and benchmarks" OFF)
option(SMARTPARALLEL_INSTALL "Generate SmartParallel install and package targets" ON)
option(SMARTPARALLEL_ENABLE_SANITIZERS
       "Enable AddressSanitizer and UndefinedBehaviorSanitizer for local/CI validation"
       OFF)

# Reproducible/Accurate validation is unsupported under floating-point modes
# that permit reassociation or discard IEEE NaN/infinity behavior.
if(SMARTPARALLEL_BUILD_V160_SCIENTIFIC_VALIDATION)
  string(CONCAT _smartparallel_all_cxx_flags
    "${CMAKE_CXX_FLAGS} "
    "${CMAKE_CXX_FLAGS_DEBUG} "
    "${CMAKE_CXX_FLAGS_RELEASE} "
    "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} "
    "${CMAKE_CXX_FLAGS_MINSIZEREL}")
  if(_smartparallel_all_cxx_flags MATCHES "(^|[ ;])(-ffast-math|-Ofast|/fp:fast)([ ;]|$)")
    message(FATAL_ERROR
      "SmartParallel v1.6 Reproducible/Accurate validation does not support unsafe fast-math flags")
  endif()
  unset(_smartparallel_all_cxx_flags)
endif()

if(SMARTPARALLEL_ENABLE_SANITIZERS)
  if(MSVC)
    message(FATAL_ERROR
            "SMARTPARALLEL_ENABLE_SANITIZERS currently supports GCC and Clang only")
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
  else()
    message(FATAL_ERROR
            "SMARTPARALLEL_ENABLE_SANITIZERS is unsupported by ${CMAKE_CXX_COMPILER_ID}")
  endif()
endif()

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
option(SMARTPARALLEL_BUILD_V140_ALGORITHM_VALIDATION
       "Build v1.4 parallel algorithm correctness validation" OFF)
option(SMARTPARALLEL_BUILD_V140_ALGORITHM_BENCHMARKS
       "Build v1.4 parallel algorithm benchmark suite" OFF)
option(SMARTPARALLEL_BUILD_VISION
       "Build the v1.5 semantic vision operation module" OFF)
option(SMARTPARALLEL_ENABLE_OPENCV_PROVIDER
       "Enable the optional OpenCV vision execution route" OFF)
option(SMARTPARALLEL_REQUIRE_OPENCV_PROVIDER
       "Fail configuration when the requested OpenCV provider is unavailable" OFF)
option(SMARTPARALLEL_BUILD_V150_VISION_VALIDATION
       "Build v1.5 adaptive execution-route correctness validation" OFF)
option(SMARTPARALLEL_BUILD_V150_VISION_BENCHMARKS
       "Build v1.5 adaptive execution-route benchmark suite" OFF)
option(SMARTPARALLEL_BUILD_V160_SCIENTIFIC_VALIDATION
       "Build v1.6 Scientific Foundations correctness validation" OFF)
option(SMARTPARALLEL_BUILD_V160_SCIENTIFIC_BENCHMARKS
       "Build v1.6 Scientific Foundations benchmark suite" OFF)
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

if(SMARTPARALLEL_ENABLE_OPENCV_PROVIDER)
  set(SMARTPARALLEL_BUILD_VISION ON)
endif()

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

set(SMARTPARALLEL_VISION_HAS_OPENCV 0)
if(SMARTPARALLEL_ENABLE_OPENCV_PROVIDER)
  find_package(OpenCV CONFIG QUIET COMPONENTS core imgproc)
  if(NOT OpenCV_FOUND)
    find_package(OpenCV QUIET COMPONENTS core imgproc)
  endif()
  if(OpenCV_FOUND)
    set(SMARTPARALLEL_VISION_HAS_OPENCV 1)
  elseif(SMARTPARALLEL_REQUIRE_OPENCV_PROVIDER)
    message(FATAL_ERROR
            "SMARTPARALLEL_REQUIRE_OPENCV_PROVIDER=ON but OpenCV core/imgproc was not found")
  else()
    message(WARNING "OpenCV was not found; the v1.5 vision module will use native routes only")
  endif()
endif()

if(SMARTPARALLEL_BUILD_VISION)
  add_subdirectory(vision)
endif()

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
    PATTERN "smart/vision" EXCLUDE
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

  if(SMARTPARALLEL_BUILD_VISION)
    install(
      DIRECTORY include/smart/vision/
      DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/smart/vision"
      COMPONENT Development
    )

    install(
      TARGETS smart_parallel_vision
      EXPORT SmartParallelVisionTargets
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
      "${CMAKE_CURRENT_SOURCE_DIR}/cmake/SmartParallelVisionConfig.cmake.in"
      "${CMAKE_CURRENT_BINARY_DIR}/SmartParallelVisionConfig.cmake"
      INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/SmartParallelVision"
    )

    write_basic_package_version_file(
      "${CMAKE_CURRENT_BINARY_DIR}/SmartParallelVisionConfigVersion.cmake"
      VERSION "${PROJECT_VERSION}"
      COMPATIBILITY SameMajorVersion
    )

    install(
      EXPORT SmartParallelVisionTargets
      FILE SmartParallelVisionTargets.cmake
      NAMESPACE SmartParallel::
      DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/SmartParallelVision"
      COMPONENT Development
    )

    install(
      FILES
        "${CMAKE_CURRENT_BINARY_DIR}/SmartParallelVisionConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/SmartParallelVisionConfigVersion.cmake"
      DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/SmartParallelVision"
      COMPONENT Development
    )
  endif()
endif()
