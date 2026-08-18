if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.28)
    project(RuffnecKkRepairCostsCap LANGUAGES CXX RC)
    include(CTest)
    add_subdirectory(../../third_party/PluginSDK "${CMAKE_BINARY_DIR}/_deps/PluginSDK")
    add_subdirectory(../../common "${CMAKE_BINARY_DIR}/_deps/common")
endif()

add_library(ruffneckk_repair_costs_cap SHARED
    src/plugin.cpp
    src/plugin.rc
)

target_link_libraries(ruffneckk_repair_costs_cap PRIVATE RuffnecKk::Common)
target_compile_features(ruffneckk_repair_costs_cap PRIVATE cxx_std_20)
set_target_properties(ruffneckk_repair_costs_cap PROPERTIES
    OUTPUT_NAME "d2rl-ruffneckk-repair-costs-cap"
    PREFIX ""
)

d2rlplugin_embed_config(
    TARGET ruffneckk_repair_costs_cap
    FILE "${CMAKE_CURRENT_SOURCE_DIR}/config/ruffneckk-repair-costs-cap.toml"
)

if(BUILD_TESTING)
    add_executable(ruffneckk_repair_costs_cap_tests tests/policy_tests.cpp)
    target_compile_features(ruffneckk_repair_costs_cap_tests PRIVATE cxx_std_20)
    target_include_directories(ruffneckk_repair_costs_cap_tests PRIVATE src)
    add_test(
        NAME ruffneckk.repair-costs-cap.policy
        COMMAND ruffneckk_repair_costs_cap_tests
            "${CMAKE_CURRENT_SOURCE_DIR}/config/ruffneckk-repair-costs-cap.toml"
    )
endif()
