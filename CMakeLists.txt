cmake_minimum_required(VERSION 3.20)

project(NicLoadOff LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(NICLOADOFF_ENABLE_WARNINGS "Enable compiler warnings" ON)

if(NICLOADOFF_ENABLE_WARNINGS)
    if(MSVC)
        add_compile_options(/W4 /permissive- /Zc:preprocessor)
    else()
        add_compile_options(-Wall -Wextra -Wpedantic -Werror=return-type)
    endif()
endif()

enable_testing()
find_package(Python3 COMPONENTS Interpreter REQUIRED)

add_subdirectory(sim)
add_subdirectory(tools)

add_test(
    NAME policy_static_summary_test
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/experiments/policy_baseline/tests/static_summary_regression_test.py
)
