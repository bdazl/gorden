# The roboslop engine, exposed as a static library whose interface is a set
# of C++23 modules. Game targets and tests import these modules; no headers
# are published.

roboslop_add_module_library(roboslop
    MODULES
        src/core/version.cppm
        src/core/error.cppm
)

if(ROBOSLOP_BUILD_TESTS)
    add_subdirectory(tests)
endif()
