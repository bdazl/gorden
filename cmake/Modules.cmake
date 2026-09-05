# C++23-module helpers for the engine and app targets.
#
# CMake's FILE_SET cxx_modules requires Ninja and per-target dyndep scanning.
# import std; intentionally remains off until libc++/libstdc++ ship a usable
# std module -- see docs/conventions/code-style.md (Module layout) and
# docs/build-system.md (C++23 module notes).

set(CMAKE_CXX_SCAN_FOR_MODULES ON)

# Bundle of project-wide language defaults applied to every engine/app
# target. Kept separate from roboslop_set_warnings() so callers can opt out of
# warnings without losing the language baseline (no-exceptions, JSON_NOEXCEPTION
# define, ...) or vice versa.
function(roboslop_apply_language_defaults target)
    target_compile_features(${target} PUBLIC cxx_std_23)

    if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        target_compile_options(${target} PRIVATE /EHs-c-)
    else()
        target_compile_options(${target} PRIVATE -fno-exceptions)
    endif()

    target_compile_definitions(${target} PRIVATE JSON_NOEXCEPTION)
endfunction()

# Add a static library whose interface is a set of C++23 module units.
# Usage:
#   roboslop_add_module_library(roboslop
#       MODULES src/core/version.cppm
#               src/core/error.cppm
#       SOURCES src/core/error.cpp
#   )
function(roboslop_add_module_library target)
    cmake_parse_arguments(ARG "" "" "MODULES;SOURCES" ${ARGN})

    add_library(${target} STATIC)

    if(ARG_MODULES)
        target_sources(${target}
            PUBLIC FILE_SET cxx_modules TYPE CXX_MODULES FILES ${ARG_MODULES}
        )
    endif()

    if(ARG_SOURCES)
        target_sources(${target} PRIVATE ${ARG_SOURCES})
    endif()

    roboslop_apply_language_defaults(${target})
    roboslop_set_warnings(${target})
    roboslop_apply_sanitizers(${target})
endfunction()
