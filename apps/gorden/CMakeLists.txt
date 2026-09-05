# Agent logic (observation, tools + validation, robot motion, brain) is
# its own module library so gorden_tests can exercise it without the
# window/bgfx main.
roboslop_add_module_library(gorden_agent
    MODULES
        src/agent/observation.cppm
        src/agent/tools.cppm
        src/agent/robot.cppm
)

target_link_libraries(gorden_agent PUBLIC
    roboslop
)

add_executable(gorden
    src/app/main.cpp
)

target_link_libraries(gorden PRIVATE
    roboslop
    gorden_agent
)

if(ROBOSLOP_BUILD_TESTS)
    add_subdirectory(tests)
endif()

roboslop_apply_language_defaults(gorden)
roboslop_set_warnings(gorden)
roboslop_apply_sanitizers(gorden)

include(ShaderCompile)

set(shader_src_dir ${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/src)

# Outputs land at <build-root>/assets/shaders/<backend>/ so the runtime
# asset path is stable across apps — gorden's main passes
# assetRoot = "assets" and the engine resolves shaders relative to that.
# `make run` cds into build/<preset>/ so the relative path works.
set(shader_out_dir ${CMAKE_BINARY_DIR}/assets/shaders)

roboslop_compile_shader(
    TYPE        VERTEX
    SHADERS     ${shader_src_dir}/vs_basic.sc
    VARYING     ${shader_src_dir}/varying.def.sc
    OUTPUT_DIR  ${shader_out_dir}
    TARGET_VAR  gorden_basic_vs_target
)

roboslop_compile_shader(
    TYPE        FRAGMENT
    SHADERS     ${shader_src_dir}/fs_basic.sc
    VARYING     ${shader_src_dir}/varying.def.sc
    OUTPUT_DIR  ${shader_out_dir}
    TARGET_VAR  gorden_basic_fs_target
)

roboslop_compile_shader(
    TYPE        VERTEX
    SHADERS     ${shader_src_dir}/vs_textured.sc
    VARYING     ${shader_src_dir}/varying.def.sc
    OUTPUT_DIR  ${shader_out_dir}
    TARGET_VAR  gorden_textured_vs_target
)

roboslop_compile_shader(
    TYPE        FRAGMENT
    SHADERS     ${shader_src_dir}/fs_textured.sc
    VARYING     ${shader_src_dir}/varying.def.sc
    OUTPUT_DIR  ${shader_out_dir}
    TARGET_VAR  gorden_textured_fs_target
)

# gorden loads these at startup; depending on them makes `make build`
# alone produce a runnable binary (the aggregate `shaders` target is
# not part of ALL).
add_dependencies(gorden
    ${gorden_basic_vs_target}
    ${gorden_basic_fs_target}
    ${gorden_textured_vs_target}
    ${gorden_textured_fs_target}
)
