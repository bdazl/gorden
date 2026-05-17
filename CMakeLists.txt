cmake_minimum_required(VERSION 3.30 FATAL_ERROR)

project(gorden
    VERSION     0.0.1
    DESCRIPTION "Single-player 3D game on the Roboslop engine"
    LANGUAGES   CXX
)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

set(CMAKE_CXX_STANDARD          23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS        OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# CMakePresets ships a Ninja-only build. Refuse other generators up-front so
# C++23 module scanning is never silently disabled.
if(NOT CMAKE_GENERATOR MATCHES "Ninja")
    message(FATAL_ERROR
        "Only Ninja is supported. Got '${CMAKE_GENERATOR}'. "
        "Use one of the configurations in CMakePresets.json."
    )
endif()

include(RoboslopOptions)
include(CompilerWarnings)
include(Sanitizers)
include(Modules)

enable_testing()

# Subdirectories land in later build-pass commits:
#   add_subdirectory(third_party)
#   add_subdirectory(roboslop)
#   add_subdirectory(gorden)
