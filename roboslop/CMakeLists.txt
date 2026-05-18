# The roboslop engine, exposed as a static library whose interface is a set
# of C++23 modules. Game targets and tests import these modules; no headers
# are published.

find_package(glfw3 CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(EnTT CONFIG REQUIRED)
find_package(Taskflow CONFIG REQUIRED)

roboslop_add_module_library(roboslop
    MODULES
        src/core/version.cppm
        src/core/error.cppm
        src/time/clock.cppm
        src/platform/window.cppm
        src/platform/input.cppm
        src/render/context.cppm
        src/render/shader.cppm
        src/render/mesh.cppm
        src/render/camera.cppm
        src/render/free_fly_camera.cppm
        src/render/asset_cache.cppm
        src/render/frontend.cppm
        src/render/graph.cppm
        src/scene/transform.cppm
        src/ecs/world.cppm
        src/sched/scheduler.cppm
        src/app/app.cppm
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
)

if(ROBOSLOP_BUILD_TESTS)
    add_subdirectory(tests)
endif()
