cmake_minimum_required(VERSION 3.30 FATAL_ERROR)

project(roboslop
    VERSION     0.0.1
    DESCRIPTION "Real-time rendering, simulation, and AI-integrated interactive systems"
    # C enabled for the miniaudio implementation TU (miniaudio_impl.c).
    LANGUAGES   CXX C
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

add_subdirectory(third_party)

# bgfx.cmake forces CMAKE_CXX_STANDARD to 20 globally. Our targets opt back
# into C++23 via target_compile_features(... cxx_std_23) in Modules.cmake,
# but restore the project-wide default so any later add_subdirectory()
# (engine, apps) starts from the C++23 baseline.
set(CMAKE_CXX_STANDARD 23)

add_subdirectory(engine)

# Applications built on the engine. One subdirectory per app under apps/;
# add a line here when a new app starts, not before.
add_subdirectory(apps/gorden)
