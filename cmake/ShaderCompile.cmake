# Shader-compilation helper.
#
# Thin wrapper around bgfx.cmake's bgfx_compile_shaders(). third_party/
# pulls bgfx.cmake via FetchContent and includes bgfxToolUtils.cmake; this
# file just normalises the API to roboslop's calling convention and
# aggregates outputs into a top-level `shaders` custom target so
# `make shaders` is a one-stop build of every shader the game registers.
#
# Usage:
#   roboslop_compile_shader(
#       TYPE        VERTEX|FRAGMENT|COMPUTE
#       SHADERS     vs_basic.sc fs_basic.sc
#       VARYING     varying.def.sc
#       OUTPUT_DIR  ${CMAKE_CURRENT_BINARY_DIR}/shaders   # optional
#       TARGET_VAR  my_shader_target                       # optional
#   )
#
# TARGET_VAR receives the name of the per-call custom target so a
# library or executable can add_dependencies() on exactly the shaders it
# loads at runtime (the aggregate `shaders` target is not part of ALL).
#
# Outputs land at <OUTPUT_DIR>/<profile-ext>/<name>.bin per backend. The
# active backend set follows bgfx_compile_shaders defaults (spirv + glsl on
# Linux; metal + spirv on macOS; s_5_0/s_6_0 on Windows).

if(NOT COMMAND bgfx_compile_shaders)
    message(FATAL_ERROR
        "ShaderCompile.cmake: bgfx_compile_shaders not defined. "
        "third_party/CMakeLists.txt must include bgfxToolUtils.cmake first."
    )
endif()

function(roboslop_compile_shader)
    cmake_parse_arguments(ARG "" "TYPE;VARYING;OUTPUT_DIR;TARGET_VAR" "SHADERS" ${ARGN})

    if(NOT ARG_TYPE OR NOT ARG_VARYING OR NOT ARG_SHADERS)
        message(FATAL_ERROR
            "roboslop_compile_shader: TYPE, VARYING, and SHADERS are required"
        )
    endif()

    if(NOT ARG_OUTPUT_DIR)
        set(ARG_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")
    endif()

    set(out_files "")
    bgfx_compile_shaders(
        TYPE          ${ARG_TYPE}
        SHADERS       ${ARG_SHADERS}
        VARYING_DEF   ${ARG_VARYING}
        OUTPUT_DIR    ${ARG_OUTPUT_DIR}
        OUT_FILES_VAR out_files
    )

    if(NOT TARGET shaders)
        add_custom_target(shaders)
    endif()

    string(SHA1 hash "${ARG_SHADERS}${ARG_TYPE}")
    string(SUBSTRING ${hash} 0 8 short_hash)
    set(sub_target "roboslop_shaders_${short_hash}")

    add_custom_target(${sub_target} DEPENDS ${out_files})
    add_dependencies(shaders ${sub_target})

    if(ARG_TARGET_VAR)
        set(${ARG_TARGET_VAR} ${sub_target} PARENT_SCOPE)
    endif()
endfunction()
