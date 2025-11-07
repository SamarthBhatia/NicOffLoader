find_package(yaml-cpp REQUIRED)

add_executable(placement_benchmark placement_benchmark.cc)
target_link_libraries(placement_benchmark PRIVATE nicloadoff_sim yaml-cpp::yaml-cpp)
target_include_directories(placement_benchmark PRIVATE ${PROJECT_SOURCE_DIR}/sim/include)
target_compile_features(placement_benchmark PRIVATE cxx_std_20)
