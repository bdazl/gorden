# The roboslop engine, exposed as a static library whose interface is a set
# of C++23 modules. Game targets and tests import these modules; no headers
# are published.

find_package(glfw3 CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(EnTT CONFIG REQUIRED)

roboslop_add_module_library(roboslop
    MODULES
        src/core/version.cppm
        src/core/error.cppm
        src/time/clock.cppm
        src/platform/window.cppm
        src/render/context.cppm
        src/render/shader.cppm
        src/render/mesh.cppm
        src/ecs/world.cppm
        src/app/app.cppm
)

target_link_libraries(roboslop PUBLIC
    glfw
    bgfx
    bx
    bimg
    spdlog::spdlog
    EnTT::EnTT
)

if(ROBOSLOP_BUILD_TESTS)
    add_subdirectory(tests)
endif()
