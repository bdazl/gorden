# Applies a git patch to a FetchContent checkout, idempotently.
#
#   cmake -DSOURCE_DIR=<git checkout> -DPATCH=<file> -P apply.cmake
#
# FetchContent runs PATCH_COMMAND after every download or update step, and
# an update can leave the previous patch in place, so a patch that already
# applies in reverse is treated as done rather than re-applied.
foreach(var SOURCE_DIR PATCH)
    if(NOT DEFINED ${var})
        message(FATAL_ERROR "apply.cmake: ${var} is required")
    endif()
endforeach()

execute_process(
    COMMAND git -C "${SOURCE_DIR}" apply --reverse --check "${PATCH}"
    RESULT_VARIABLE already_applied
    OUTPUT_QUIET ERROR_QUIET
)
if(already_applied EQUAL 0)
    message(STATUS "patch already applied: ${PATCH}")
    return()
endif()

execute_process(
    COMMAND git -C "${SOURCE_DIR}" apply "${PATCH}"
    RESULT_VARIABLE rc
)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "failed to apply ${PATCH} in ${SOURCE_DIR}")
endif()
message(STATUS "applied patch: ${PATCH}")
