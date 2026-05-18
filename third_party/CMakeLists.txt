# Third-party dependencies that aren't on Conan Center.
#
# Sources are fetched at configure time via FetchContent; everything lands
# under build/<preset>/_deps/ which is gitignored. Commits pin upstream by
# SHA or tag so every clean configure produces the same dependency state.
#
# See docs/build-system.md ("Adding a dependency") for the decision tree.

include(FetchContent)

# Tags / SHAs are exposed as cache vars so they can be overridden from the
# command line for one-off bisects without editing the file.
set(BGFX_CMAKE_GIT_TAG "cf47cdd6b2dd1b45d19450b2b4c3a2267b6a9e87"
    CACHE STRING "bkaradzic/bgfx.cmake commit to fetch"
)
set(MINIAUDIO_GIT_TAG  "0.11.25"
    CACHE STRING "mackron/miniaudio tag to fetch"
)

# --- bgfx (+ bx, bimg, shaderc) -------------------------------------------
# bkaradzic/bgfx.cmake wraps bgfx/bx/bimg with a CMake build, pulls them as
# its own submodules at configure time, and exposes both library targets
# (bgfx, bx, bimg) and the shaderc executable used by ShaderCompile.cmake.

set(BGFX_BUILD_EXAMPLES OFF CACHE BOOL "")
set(BGFX_BUILD_TESTS    OFF CACHE BOOL "")
set(BGFX_INSTALL        OFF CACHE BOOL "")

FetchContent_Declare(
    bgfx_cmake
    GIT_REPOSITORY https://github.com/bkaradzic/bgfx.cmake.git
    GIT_TAG        ${BGFX_CMAKE_GIT_TAG}
    GIT_SUBMODULES_RECURSE TRUE
)
FetchContent_MakeAvailable(bgfx_cmake)

# bgfxToolUtils ships bgfx_compile_shaders() but isn't auto-included by
# bgfx.cmake's main CMakeLists. Pull it in so cmake/ShaderCompile.cmake can
# wrap it from anywhere in the tree.
include(${bgfx_cmake_SOURCE_DIR}/cmake/bgfxToolUtils.cmake)

# bgfxToolUtils' bgfx_compile_shaders() reads BGFX_SHADER_INCLUDE_PATH for
# shader -i resolution. The Conan-flavoured install path sets it, but a
# FetchContent build does not -- point it at the fetched bgfx source so
# `#include <bgfx_shader.sh>` resolves out of the box.
set(BGFX_SHADER_INCLUDE_PATH "${bgfx_cmake_SOURCE_DIR}/bgfx/src"
    CACHE PATH "bgfx shader include path" FORCE
)

# --- miniaudio ------------------------------------------------------------
# Single-header audio library. Exposed as an INTERFACE target; the audio
# subsystem must #define MINIAUDIO_IMPLEMENTATION in exactly one TU.

FetchContent_Declare(
    miniaudio
    GIT_REPOSITORY https://github.com/mackron/miniaudio.git
    GIT_TAG        ${MINIAUDIO_GIT_TAG}
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(miniaudio)

add_library(roboslop_miniaudio INTERFACE)
# SYSTEM so the engine's strict warning set (-Wold-style-cast,
# -Wsign-conversion, ...) does not flag miniaudio's C-style internals
# when the header is included from a roboslop module.
target_include_directories(roboslop_miniaudio SYSTEM INTERFACE ${miniaudio_SOURCE_DIR})
add_library(roboslop::miniaudio ALIAS roboslop_miniaudio)

# --- imgui_impl_bgfx ------------------------------------------------------
# Deferred. No actively maintained, single-source fork verified yet; the
# dev-UI subsystem will land its own minimal implementation linking
# imgui::imgui (Conan) + bgfx (above) when ROBOSLOP_DEV_UI=ON code arrives.
