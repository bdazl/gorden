# Shader Lab: live shader development sandbox (roadmap M1). Requires the
# dev UI; the top-level CMakeLists only adds this directory when
# ROBOSLOP_DEV_UI is ON.

add_executable(shaderlab
    src/app/main.cpp
)

target_sources(shaderlab
    PRIVATE FILE_SET cxx_modules TYPE CXX_MODULES FILES
        src/lab/shader_watcher.cppm
        src/lab/reload.cppm
)

target_link_libraries(shaderlab PRIVATE
    roboslop
)

roboslop_apply_language_defaults(shaderlab)
roboslop_set_warnings(shaderlab)
roboslop_apply_sanitizers(shaderlab)

include(ShaderCompile)

set(shader_src_dir ${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/src)
set(shader_out_dir ${CMAKE_BINARY_DIR}/assets/shaders)

# Build-time compile of the lab pair so a fresh start always has a valid
# program to show (and so `make build` validates the checked-in
# sources). Runtime recompiles go through roboslop.assets.shader_compiler
# and never touch these outputs.
roboslop_compile_shader(
    TYPE        VERTEX
    SHADERS     ${shader_src_dir}/vs_lab.sc
    VARYING     ${shader_src_dir}/varying.def.sc
    OUTPUT_DIR  ${shader_out_dir}
    TARGET_VAR  lab_vs_target
)

roboslop_compile_shader(
    TYPE        FRAGMENT
    SHADERS     ${shader_src_dir}/fs_lab.sc
    VARYING     ${shader_src_dir}/varying.def.sc
    OUTPUT_DIR  ${shader_out_dir}
    TARGET_VAR  lab_fs_target
)

add_dependencies(shaderlab ${lab_vs_target} ${lab_fs_target})

# The runtime compiler needs the shaderc binary bgfx.cmake built, the
# bgfx shader include directory (bgfx_shader.sh), and the source tree
# to watch. Baked in as absolute paths: Shader Lab is a developer tool
# that runs out of the build tree, not a shipped binary.
target_compile_definitions(shaderlab PRIVATE
    "SHADERLAB_SHADERC_PATH=\"$<TARGET_FILE:shaderc>\""
    "SHADERLAB_BGFX_SHADER_INCLUDE=\"${BGFX_SHADER_INCLUDE_PATH}\""
    "SHADERLAB_SHADER_SOURCE_DIR=\"${shader_src_dir}\""
)
