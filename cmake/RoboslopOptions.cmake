# Project-wide CMake options.
# Toggle these on the configure command line or via CMakePresets cacheVariables.

option(ROBOSLOP_DEV_UI    "Enable ImGui-based developer UI"                ON)
option(ROBOSLOP_BUILD_TESTS "Build Catch2 tests for the roboslop engine"   ON)

# Semicolon-separated list of sanitisers to apply to roboslop targets.
# Honoured by Sanitizers.cmake. Valid entries: asan, ubsan, tsan, msan.
set(ROBOSLOP_SANITIZERS "" CACHE STRING
    "Sanitisers to apply to roboslop targets (semicolon-separated)"
)
