# The roboslop engine, exposed as a static library whose interface is a set
# of C++23 modules. App targets and tests import these modules; no headers
# are published.

find_package(glfw3 CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(EnTT CONFIG REQUIRED)
find_package(Taskflow CONFIG REQUIRED)
find_package(Jolt CONFIG REQUIRED)
find_package(assimp CONFIG REQUIRED)
find_package(stb REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(CURL CONFIG REQUIRED)

roboslop_add_module_library(roboslop
    MODULES
        src/core/version.cppm
        src/core/error.cppm
        src/core/file.cppm
        src/core/paths.cppm
        src/core/json_file.cppm
        src/time/clock.cppm
        src/platform/window.cppm
        src/platform/input.cppm
        src/platform/process.cppm
        src/platform/http.cppm
        src/render/context.cppm
        src/render/shader.cppm
        src/render/mesh.cppm
        src/render/primitives.cppm
        src/render/material.cppm
        src/render/camera.cppm
        src/render/free_fly_camera.cppm
        src/render/asset_cache.cppm
        src/render/frontend.cppm
        src/render/graph.cppm
        src/render/lighting.cppm
        src/physics/components.cppm
        src/physics/jolt_world.cppm
        src/assets/mesh_loader.cppm
        src/assets/texture_loader.cppm
        src/assets/shader_compiler.cppm
        src/audio/audio.cppm
        src/audio/audio_device.cppm
        src/animation/clip.cppm
        src/animation/skeleton.cppm
        src/animation/state.cppm
        src/scene/transform.cppm
        src/ecs/world.cppm
        src/sched/scheduler.cppm
        src/vfs/vfs.cppm
        src/ui/dev_ui.cppm
        src/llm/llm.cppm
        src/llm/openai_wire.cppm
        src/llm/backend.cppm
        src/llm/backend_scripted.cppm
        src/llm/backend_openai.cppm
        src/app/app.cppm
    SOURCES
        src/assets/stb_image_impl.cpp
        src/audio/miniaudio_impl.c
        src/ui/imgui_bgfx_renderer.cpp
)

# miniaudio_impl.c is the single TU that defines MINIAUDIO_IMPLEMENTATION;
# it pulls in the entire single-header library, which uses idioms the
# engine's strict warning set (Werror, sign-conversion, double-promotion,
# format=2, ...) flags hundreds of times. Silence warnings for this one
# file rather than weakening the project-wide flags.
set_source_files_properties(
    src/audio/miniaudio_impl.c
    PROPERTIES COMPILE_OPTIONS "-w"
)

# --- Dev UI (Dear ImGui) -------------------------------------------------
# roboslop.ui is always built: imgui is a declared Conan dependency and
# keeping the module unconditional avoids #if-guarded members in App.
# ROBOSLOP_DEV_UI decides whether App honours AppConfig::enableDevUi
# (and, at the top level, whether the shaderlab app is configured).
#
# The GLFW platform backend is compiled straight from the Conan
# package's res/bindings — same imgui version, no vendored copy. The
# bgfx renderer backend is ours (src/ui/imgui_bgfx_renderer.cpp).
string(TOUPPER "${CMAKE_BUILD_TYPE}" roboslop_cfg_upper)
set(imgui_bindings_dir "${imgui_PACKAGE_FOLDER_${roboslop_cfg_upper}}/res/bindings")
if(NOT EXISTS "${imgui_bindings_dir}/imgui_impl_glfw.cpp")
    message(FATAL_ERROR
        "imgui_impl_glfw.cpp not found at ${imgui_bindings_dir}; "
        "the Conan imgui package layout changed."
    )
endif()
target_sources(roboslop PRIVATE "${imgui_bindings_dir}/imgui_impl_glfw.cpp")
# Third-party TU: same treatment as miniaudio_impl.c.
set_source_files_properties(
    "${imgui_bindings_dir}/imgui_impl_glfw.cpp"
    PROPERTIES COMPILE_OPTIONS "-w"
)
target_include_directories(roboslop SYSTEM PRIVATE "${imgui_bindings_dir}")
if(ROBOSLOP_DEV_UI)
    target_compile_definitions(roboslop PUBLIC ROBOSLOP_DEV_UI=1)
else()
    target_compile_definitions(roboslop PUBLIC ROBOSLOP_DEV_UI=0)
endif()

# Engine-owned shaders (the ImGui pair) compile into the same
# <build-root>/assets/shaders/<backend>/ tree app shaders use, so
# AssetCache::program("vs_imgui", "fs_imgui") resolves from any app's
# asset root. The engine target depends on them so `make build` alone
# yields a runnable dev UI.
include(ShaderCompile)
set(ui_shader_src ${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/src)
roboslop_compile_shader(
    TYPE        VERTEX
    SHADERS     ${ui_shader_src}/vs_imgui.sc
    VARYING     ${ui_shader_src}/varying.def.sc
    OUTPUT_DIR  ${CMAKE_BINARY_DIR}/assets/shaders
    TARGET_VAR  ui_vs_target
)
roboslop_compile_shader(
    TYPE        FRAGMENT
    SHADERS     ${ui_shader_src}/fs_imgui.sc
    VARYING     ${ui_shader_src}/varying.def.sc
    OUTPUT_DIR  ${CMAKE_BINARY_DIR}/assets/shaders
    TARGET_VAR  ui_fs_target
)
add_dependencies(roboslop ${ui_vs_target} ${ui_fs_target})

target_link_libraries(roboslop PUBLIC
    glfw
    glm::glm
    bgfx
    bx
    bimg
    spdlog::spdlog
    EnTT::EnTT
    Taskflow::Taskflow
    Jolt::Jolt
    assimp::assimp
    stb::stb
    roboslop::miniaudio
    imgui::imgui
    nlohmann_json::nlohmann_json
    CURL::libcurl
)

if(ROBOSLOP_BUILD_TESTS)
    add_subdirectory(tests)
endif()
