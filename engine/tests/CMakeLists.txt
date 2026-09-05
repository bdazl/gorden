find_package(Catch2 3 CONFIG REQUIRED)

add_executable(roboslop_tests
    version_test.cpp
    time_clock_test.cpp
    ecs_world_test.cpp
    transform_test.cpp
    render_camera_test.cpp
    input_test.cpp
    process_test.cpp
    shader_compiler_test.cpp
    free_fly_camera_test.cpp
    scheduler_test.cpp
    render_graph_test.cpp
    frame_arena_test.cpp
    physics_test.cpp
    lighting_test.cpp
    animation_test.cpp
)

# Same language baseline as the engine so the imported PCMs match the
# compiler config exactly (a mismatch would refuse to load the .pcm).
roboslop_apply_language_defaults(roboslop_tests)

target_link_libraries(roboslop_tests PRIVATE
    roboslop
    Catch2::Catch2WithMain
)

roboslop_set_warnings(roboslop_tests)
roboslop_apply_sanitizers(roboslop_tests)

include(Catch)
catch_discover_tests(roboslop_tests)
