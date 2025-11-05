find_package(Curses REQUIRED)

add_executable(nicloadoff_tui main.cc)
target_link_libraries(nicloadoff_tui PRIVATE nicloadoff_sim ${CURSES_LIBRARIES})
target_include_directories(nicloadoff_tui PRIVATE
    ${CURSES_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)
target_compile_features(nicloadoff_tui PRIVATE cxx_std_20)
