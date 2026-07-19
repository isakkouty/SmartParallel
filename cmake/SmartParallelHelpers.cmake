function(smartparallel_apply_warnings target_name)
  if(MSVC)
    target_compile_options(${target_name} PRIVATE /W4)
  else()
    target_compile_options(${target_name} PRIVATE -Wall -Wextra -Wpedantic)
  endif()
endfunction()

function(smartparallel_copy_tbb_runtime target_name)
  if(WIN32)
    add_custom_command(
      TARGET ${target_name}
      POST_BUILD
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different
              "$<TARGET_FILE:TBB::tbb>"
              "$<TARGET_FILE_DIR:${target_name}>"
      COMMENT "Copying oneTBB runtime for ${target_name}"
      VERBATIM
    )
  endif()
endfunction()
