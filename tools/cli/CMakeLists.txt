find_package(yaml-cpp REQUIRED)

add_library(nicloadoff_cli_lib STATIC runner.cc)
target_link_libraries(nicloadoff_cli_lib PUBLIC nicloadoff_sim yaml-cpp::yaml-cpp)
target_compile_features(nicloadoff_cli_lib PUBLIC cxx_std_20)
target_include_directories(nicloadoff_cli_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(nicloadoff_cli main.cc)
target_link_libraries(nicloadoff_cli PRIVATE nicloadoff_cli_lib)
target_compile_features(nicloadoff_cli PRIVATE cxx_std_20)

add_subdirectory(tests)
