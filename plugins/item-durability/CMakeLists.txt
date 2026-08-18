if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.28)
    project(RuffnecKkItemDurability LANGUAGES CXX RC)
    include(CTest)
    add_subdirectory(../../third_party/PluginSDK "${CMAKE_BINARY_DIR}/_deps/PluginSDK")
    add_subdirectory(../../common "${CMAKE_BINARY_DIR}/_deps/common")
endif()

add_library(ruffneckk_item_durability SHARED
    src/plugin.cpp
    src/plugin.rc
)

target_link_libraries(ruffneckk_item_durability PRIVATE RuffnecKk::Common)
target_compile_features(ruffneckk_item_durability PRIVATE cxx_std_20)
set_target_properties(ruffneckk_item_durability PROPERTIES
    OUTPUT_NAME "d2rl-ruffneckk-item-durability"
    PREFIX ""
)

d2rlplugin_embed_config(
    TARGET ruffneckk_item_durability
    FILE "${CMAKE_CURRENT_SOURCE_DIR}/config/ruffneckk-item-durability.toml"
)

if(BUILD_TESTING)
    add_executable(ruffneckk_item_durability_tests tests/policy_tests.cpp)
    target_compile_features(ruffneckk_item_durability_tests PRIVATE cxx_std_20)
    target_include_directories(ruffneckk_item_durability_tests PRIVATE src)
    add_test(
        NAME ruffneckk.item-durability.policy
        COMMAND ruffneckk_item_durability_tests
            "${CMAKE_CURRENT_SOURCE_DIR}/config/ruffneckk-item-durability.toml"
    )
endif()
