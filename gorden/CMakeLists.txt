find_package(spdlog CONFIG REQUIRED)

add_executable(gorden
    src/app/main.cpp
)

target_link_libraries(gorden PRIVATE
    roboslop
    spdlog::spdlog
)

roboslop_apply_language_defaults(gorden)
roboslop_set_warnings(gorden)
roboslop_apply_sanitizers(gorden)
