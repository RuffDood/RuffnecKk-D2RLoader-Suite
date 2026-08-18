if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.28)
    project(RuffnecKkEtherealItemRules LANGUAGES CXX RC)
    include(CTest)
    add_subdirectory(../../third_party/PluginSDK "${CMAKE_BINARY_DIR}/_deps/PluginSDK")
    add_subdirectory(../../common "${CMAKE_BINARY_DIR}/_deps/common")
endif()

add_library(ruffneckk_ethereal_item_rules SHARED
    src/plugin.cpp
    src/plugin.rc
)

target_link_libraries(ruffneckk_ethereal_item_rules PRIVATE RuffnecKk::Common)
target_compile_features(ruffneckk_ethereal_item_rules PRIVATE cxx_std_20)
set_target_properties(ruffneckk_ethereal_item_rules PROPERTIES
    OUTPUT_NAME "d2rl-ruffneckk-ethereal-item-rules"
    PREFIX ""
)

d2rlplugin_embed_config(
    TARGET ruffneckk_ethereal_item_rules
    FILE "${CMAKE_CURRENT_SOURCE_DIR}/config/ruffneckk-ethereal-item-rules.toml"
)

if(BUILD_TESTING)
    add_executable(ruffneckk_ethereal_item_rules_tests tests/policy_tests.cpp)
    target_compile_features(ruffneckk_ethereal_item_rules_tests PRIVATE cxx_std_20)
    target_include_directories(ruffneckk_ethereal_item_rules_tests PRIVATE src)
    add_test(
        NAME ruffneckk.ethereal-item-rules.policy
        COMMAND ruffneckk_ethereal_item_rules_tests
            "${CMAKE_CURRENT_SOURCE_DIR}/config/ruffneckk-ethereal-item-rules.toml"
    )
endif()
