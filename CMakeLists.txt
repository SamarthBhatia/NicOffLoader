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

add_test(
    NAME policy_batch_acceptance_test
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/experiments/policy_baseline/tests/policy_batch_acceptance_test.py
            --cli $<TARGET_FILE:nicloadoff_cli>
            --manifest ${CMAKE_SOURCE_DIR}/experiments/policy_baseline/batch.yaml
            --csv ${CMAKE_SOURCE_DIR}/experiments/policy_baseline/results/policy_baseline.csv
            --expected ${CMAKE_SOURCE_DIR}/experiments/policy_baseline/tests/data/skew_tier_baselines_expected.csv
)

add_test(
    NAME rolling_metrics_spike_test
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tools/cli/tests/rolling_metrics_spike_test.py
            --cli $<TARGET_FILE:nicloadoff_cli>
            --profile ${CMAKE_SOURCE_DIR}/profiles/bf2_default.yaml
            --workload ${CMAKE_SOURCE_DIR}/workloads/tests/rolling_spike.yaml
)

add_test(
    NAME rolling_metrics_nic_spike_test
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tools/cli/tests/rolling_metrics_nic_spike_test.py
            --cli $<TARGET_FILE:nicloadoff_cli>
            --profile ${CMAKE_SOURCE_DIR}/profiles/bf2_default.yaml
            --workload ${CMAKE_SOURCE_DIR}/workloads/tests/rolling_spike_nic.yaml
)

add_custom_target(
    policy_analysis
    COMMAND ${CMAKE_SOURCE_DIR}/experiments/policy_baseline/run_analysis.sh
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Export/plot/pandas policy baseline analysis outputs"
)
