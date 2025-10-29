set(SIM_SOURCES src/event_queue.cc)

add_library(nicloadoff_sim STATIC ${SIM_SOURCES})
target_include_directories(
  nicloadoff_sim
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(nicloadoff_sim PUBLIC cxx_std_20)

add_subdirectory(tests)
