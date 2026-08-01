#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "SmartParallel::smart_parallel" for configuration "Release"
set_property(TARGET SmartParallel::smart_parallel APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(SmartParallel::smart_parallel PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/smart_parallel.lib"
  )

list(APPEND _cmake_import_check_targets SmartParallel::smart_parallel )
list(APPEND _cmake_import_check_files_for_SmartParallel::smart_parallel "${_IMPORT_PREFIX}/lib/smart_parallel.lib" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
