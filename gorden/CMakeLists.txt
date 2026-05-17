add_executable(gorden
    src/app/main.cpp
)

target_link_libraries(gorden PRIVATE
    roboslop
)

roboslop_apply_language_defaults(gorden)
roboslop_set_warnings(gorden)
roboslop_apply_sanitizers(gorden)

include(ShaderCompile)

set(shader_src_dir ${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/src)
set(shader_out_dir ${CMAKE_CURRENT_BINARY_DIR}/assets/shaders)

roboslop_compile_shader(
    TYPE        VERTEX
    SHADERS     ${shader_src_dir}/vs_basic.sc
    VARYING     ${shader_src_dir}/varying.def.sc
    OUTPUT_DIR  ${shader_out_dir}
)

roboslop_compile_shader(
    TYPE        FRAGMENT
    SHADERS     ${shader_src_dir}/fs_basic.sc
    VARYING     ${shader_src_dir}/varying.def.sc
    OUTPUT_DIR  ${shader_out_dir}
)
