find_package(Curses REQUIRED)
find_package(yaml-cpp REQUIRED)

add_executable(nicloadoff_tui main.cc)
target_link_libraries(nicloadoff_tui PRIVATE nicloadoff_sim ${CURSES_LIBRARIES} yaml-cpp::yaml-cpp)
target_include_directories(nicloadoff_tui PRIVATE ${CURSES_INCLUDE_DIR})
target_compile_features(nicloadoff_tui PRIVATE cxx_std_20)
