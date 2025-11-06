set(SIM_SOURCES
    src/event_queue.cc
    src/dag_submission_controller.cc
    src/dag_runtime.cc
    src/resource.cc
    src/scheduler.cc
    src/profile_resources.cc
    src/service_time_model.cc
    src/workload.cc
    src/workload_loader.cc
    src/profile_loader.cc
    src/run_metrics.cc
)

add_library(nicloadoff_sim STATIC ${SIM_SOURCES})
target_include_directories(nicloadoff_sim PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(nicloadoff_sim PUBLIC cxx_std_20)

add_subdirectory(tests)
