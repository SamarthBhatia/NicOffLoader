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
    add_compile_options(
      -Wall
      -Wextra
      -Wpedantic
      -Werror=return-type
    )
  endif()
endif()

enable_testing()

add_subdirectory(sim)
