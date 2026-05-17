# Per-target sanitiser flag application. Driven by ROBOSLOP_SANITIZERS cache
# variable (set by presets asan-ubsan and tsan).
#
# Sanitisers are applied to *our* targets only. Dependency packages that need
# rebuilding under sanitisers (notably tsan) are handled at the Conan profile
# level, not here.

function(roboslop_apply_sanitizers target)
    if(NOT ROBOSLOP_SANITIZERS)
        return()
    endif()

    set(flags "")
    foreach(san IN LISTS ROBOSLOP_SANITIZERS)
        if(san STREQUAL "asan")
            list(APPEND flags -fsanitize=address -fno-omit-frame-pointer)
        elseif(san STREQUAL "ubsan")
            list(APPEND flags -fsanitize=undefined)
        elseif(san STREQUAL "tsan")
            list(APPEND flags -fsanitize=thread)
        elseif(san STREQUAL "msan")
            list(APPEND flags -fsanitize=memory -fno-omit-frame-pointer)
        else()
            message(FATAL_ERROR "Unknown sanitiser '${san}' in ROBOSLOP_SANITIZERS")
        endif()
    endforeach()

    target_compile_options(${target} PRIVATE ${flags})
    target_link_options(${target}    PRIVATE ${flags})
endfunction()
