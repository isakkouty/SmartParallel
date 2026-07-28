add_library(
  smart_parallel_vision
  src/threshold.cpp
  src/threshold_kernel_dispatch.cpp
  src/threshold_kernel_x86_features.cpp
  src/threshold_kernel_sse2.cpp
  src/threshold_kernel_avx2.cpp
)

add_library(SmartParallel::vision ALIAS smart_parallel_vision)

set_target_properties(
  smart_parallel_vision
  PROPERTIES
    EXPORT_NAME vision
    VERSION "${PROJECT_VERSION}"
    SOVERSION "${PROJECT_VERSION_MAJOR}"
)

target_compile_features(smart_parallel_vision PUBLIC cxx_std_17)
target_include_directories(
  smart_parallel_vision
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/generated>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(smart_parallel_vision PUBLIC SmartParallel::smart_parallel)

if(SMARTPARALLEL_VISION_HAS_OPENCV)
  target_sources(smart_parallel_vision PRIVATE src/opencv_provider.cpp)
  target_compile_definitions(smart_parallel_vision PUBLIC SMARTPARALLEL_VISION_HAS_OPENCV=1)
  if(TARGET opencv_core AND TARGET opencv_imgproc)
    target_link_libraries(smart_parallel_vision PRIVATE opencv_core opencv_imgproc)
  else()
    target_include_directories(smart_parallel_vision PRIVATE ${OpenCV_INCLUDE_DIRS})
    target_link_libraries(smart_parallel_vision PRIVATE ${OpenCV_LIBS})
  endif()
else()
  target_sources(smart_parallel_vision PRIVATE src/opencv_provider_stub.cpp)
  target_compile_definitions(smart_parallel_vision PUBLIC SMARTPARALLEL_VISION_HAS_OPENCV=0)
endif()

# The SIMD kernels live in isolated translation units so the rest of the
# library remains compatible with the baseline architecture. Runtime CPU
# detection prevents unsupported instructions from being executed.
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64|i[3-6]86|x86)$")
  if(MSVC)
    set_source_files_properties(
      src/threshold_kernel_x86_features.cpp
      PROPERTIES COMPILE_OPTIONS "/arch:AVX"
    )
    set_source_files_properties(
      src/threshold_kernel_avx2.cpp
      PROPERTIES COMPILE_OPTIONS "/arch:AVX2"
    )
    if(CMAKE_SIZEOF_VOID_P EQUAL 4)
      set_source_files_properties(
        src/threshold_kernel_sse2.cpp
        PROPERTIES COMPILE_OPTIONS "/arch:SSE2"
      )
    endif()
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set_source_files_properties(
      src/threshold_kernel_sse2.cpp
      PROPERTIES COMPILE_OPTIONS "-msse2"
    )
    set_source_files_properties(
      src/threshold_kernel_avx2.cpp
      PROPERTIES COMPILE_OPTIONS "-mavx2"
    )
  endif()
endif()

smartparallel_apply_warnings(smart_parallel_vision)
smartparallel_copy_tbb_runtime(smart_parallel_vision)
