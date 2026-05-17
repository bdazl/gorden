# Per-target warning flags. Applied to engine + game targets via
# roboslop_apply_language_defaults() in Modules.cmake; third-party targets are
# left alone.

function(roboslop_set_warnings target)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR
       CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_compile_options(${target} PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wshadow
            -Wconversion
            -Wsign-conversion
            -Wnon-virtual-dtor
            -Wold-style-cast
            -Wcast-align
            -Woverloaded-virtual
            -Wnull-dereference
            -Wdouble-promotion
            -Wformat=2
            -Wimplicit-fallthrough
            # Clang 22+ warns on __COUNTER__ as a future-C2y feature even
            # though it has been a de-facto extension for decades; third-
            # party headers (Catch2, others) rely on it.
            -Wno-c2y-extensions
            -Werror
        )
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        target_compile_options(${target} PRIVATE /W4 /permissive- /WX)
    endif()
endfunction()
