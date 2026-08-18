if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.28)
    project(RuffnecKkProgressiveAffixes LANGUAGES CXX RC)
    include(CTest)
    add_subdirectory(../../third_party/PluginSDK "${CMAKE_BINARY_DIR}/_deps/PluginSDK")
    add_subdirectory(../../common "${CMAKE_BINARY_DIR}/_deps/common")
endif()

add_library(ruffneckk_progressive_affixes SHARED
    src/plugin.cpp
    src/plugin.rc
)

target_link_libraries(ruffneckk_progressive_affixes PRIVATE RuffnecKk::Common)
target_compile_features(ruffneckk_progressive_affixes PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(ruffneckk_progressive_affixes PRIVATE /EHsc)
endif()
set_target_properties(ruffneckk_progressive_affixes PROPERTIES
    OUTPUT_NAME "d2rl-ruffneckk-progressive-affixes"
    PREFIX ""
)

d2rlplugin_embed_config(
    TARGET ruffneckk_progressive_affixes
    FILE "${CMAKE_CURRENT_SOURCE_DIR}/config/ruffneckk-progressive-affixes.toml"
)

if(BUILD_TESTING)
    add_executable(ruffneckk_progressive_affixes_tests
        tests/policy_tests.cpp
    )
    target_link_libraries(ruffneckk_progressive_affixes_tests PRIVATE RuffnecKk::Common)
    target_compile_features(ruffneckk_progressive_affixes_tests PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(ruffneckk_progressive_affixes_tests PRIVATE /EHsc)
    endif()
    target_include_directories(ruffneckk_progressive_affixes_tests PRIVATE src)
    add_test(
        NAME ruffneckk.progressive-affixes.policy
        COMMAND ruffneckk_progressive_affixes_tests
            "${CMAKE_CURRENT_SOURCE_DIR}/config/ruffneckk-progressive-affixes.toml"
    )
endif()
