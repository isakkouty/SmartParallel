if(SMARTPARALLEL_BUILD_V170_TOOLS)
  add_executable(smartparallel_profile profile_tool.cpp)
  target_link_libraries(smartparallel_profile PRIVATE SmartParallel::smart_parallel)
  if(TARGET smart_parallel_vision)
    target_link_libraries(smartparallel_profile PRIVATE SmartParallel::vision)
    target_compile_definitions(smartparallel_profile PRIVATE SMARTPARALLEL_TOOL_HAS_VISION=1)
  else()
    target_compile_definitions(smartparallel_profile PRIVATE SMARTPARALLEL_TOOL_HAS_VISION=0)
  endif()
  smartparallel_apply_warnings(smartparallel_profile)
  smartparallel_copy_tbb_runtime(smartparallel_profile)

  add_executable(smartparallel_calibrate calibrate.cpp)
  target_link_libraries(smartparallel_calibrate PRIVATE SmartParallel::smart_parallel)
  if(TARGET smart_parallel_vision)
    target_link_libraries(smartparallel_calibrate PRIVATE SmartParallel::vision)
    target_compile_definitions(smartparallel_calibrate PRIVATE SMARTPARALLEL_TOOL_HAS_VISION=1)
  else()
    target_compile_definitions(smartparallel_calibrate PRIVATE SMARTPARALLEL_TOOL_HAS_VISION=0)
  endif()
  smartparallel_apply_warnings(smartparallel_calibrate)
  smartparallel_copy_tbb_runtime(smartparallel_calibrate)

  add_executable(smartparallel_replay replay.cpp)
  target_link_libraries(smartparallel_replay PRIVATE SmartParallel::smart_parallel)
  smartparallel_apply_warnings(smartparallel_replay)
  smartparallel_copy_tbb_runtime(smartparallel_replay)

  # Installed CLI executables must locate installed shared SmartParallel
  # libraries without environment variables. This is a no-op for static builds.
  if(APPLE)
    set(_smartparallel_tool_install_rpath "@loader_path/../${CMAKE_INSTALL_LIBDIR}")
  elseif(UNIX)
    set(_smartparallel_tool_install_rpath "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
  endif()
  if(DEFINED _smartparallel_tool_install_rpath)
    set_target_properties(
      smartparallel_profile smartparallel_calibrate smartparallel_replay
      PROPERTIES INSTALL_RPATH "${_smartparallel_tool_install_rpath}"
    )
  endif()

  install(TARGETS smartparallel_profile smartparallel_calibrate smartparallel_replay
          RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}" COMPONENT Runtime)

  # Build-tree executables receive the oneTBB DLL through
  # smartparallel_copy_tbb_runtime(). Installed CLI tools need the same
  # runtime beside them; otherwise Windows fails before main() with
  # STATUS_DLL_NOT_FOUND. Keep this scoped to the Windows/TBB package.
  if(WIN32 AND TBB_FOUND)
    install(FILES "$<TARGET_FILE:TBB::tbb>"
            DESTINATION "${CMAKE_INSTALL_BINDIR}"
            COMPONENT Runtime)
  endif()
endif()
