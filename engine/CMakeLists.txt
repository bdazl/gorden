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

roboslop_add_module_library(roboslop
    MODULES
        src/core/version.cppm
        src/core/error.cppm
        src/core/file.cppm
        src/time/clock.cppm
        src/platform/window.cppm
        src/platform/input.cppm
        src/platform/process.cppm
        src/render/context.cppm
        src/render/shader.cppm
        src/render/mesh.cppm
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
        src/app/app.cppm
    SOURCES
        src/assets/stb_image_impl.cpp
        src/audio/miniaudio_impl.c
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
)

if(ROBOSLOP_BUILD_TESTS)
    add_subdirectory(tests)
endif()
