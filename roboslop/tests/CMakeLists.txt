find_package(Catch2 3 CONFIG REQUIRED)

add_executable(roboslop_tests
    version_test.cpp
    time_clock_test.cpp
    ecs_world_test.cpp
    transform_test.cpp
    render_camera_test.cpp
    input_test.cpp
    free_fly_camera_test.cpp
)

# Tests must use the same -fno-exceptions code-gen as the engine so the
# imported PCMs match the compiler config exactly (a configuration mismatch
# would refuse to load the .pcm). CATCH_CONFIG_DISABLE_EXCEPTIONS makes
# Catch2's macros expand to a TRY/CATCH abstraction that compiles cleanly
# under -fno-exceptions.
roboslop_apply_language_defaults(roboslop_tests)
target_compile_definitions(roboslop_tests PRIVATE CATCH_CONFIG_DISABLE_EXCEPTIONS)

target_link_libraries(roboslop_tests PRIVATE
    roboslop
    Catch2::Catch2WithMain
)

roboslop_set_warnings(roboslop_tests)
roboslop_apply_sanitizers(roboslop_tests)

include(Catch)
catch_discover_tests(roboslop_tests)
