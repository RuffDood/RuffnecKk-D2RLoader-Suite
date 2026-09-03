if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.28)
    project(RuffnecKkMassIdentify LANGUAGES CXX RC)
    include(CTest)
    add_subdirectory(../../third_party/PluginSDK "${CMAKE_BINARY_DIR}/_deps/PluginSDK")
    add_subdirectory(../../common "${CMAKE_BINARY_DIR}/_deps/common")
endif()

add_library(ruffneckk_mass_identify SHARED
    src/plugin.cpp
    src/plugin.rc
)

target_link_libraries(ruffneckk_mass_identify PRIVATE RuffnecKk::Common user32)
target_compile_features(ruffneckk_mass_identify PRIVATE cxx_std_20)
set_target_properties(ruffneckk_mass_identify PROPERTIES
    OUTPUT_NAME "d2rl-ruffneckk-mass-identify"
    PREFIX ""
)

d2rlplugin_embed_config(
    TARGET ruffneckk_mass_identify
    FILE "${CMAKE_CURRENT_SOURCE_DIR}/config/ruffneckk-mass-identify.toml"
)

if(BUILD_TESTING)
    add_executable(ruffneckk_mass_identify_tests tests/policy_tests.cpp)
    target_compile_features(ruffneckk_mass_identify_tests PRIVATE cxx_std_20)
    target_include_directories(ruffneckk_mass_identify_tests PRIVATE src)
    add_test(
        NAME ruffneckk.mass-identify.policy
        COMMAND ruffneckk_mass_identify_tests
            "${CMAKE_CURRENT_SOURCE_DIR}/config/ruffneckk-mass-identify.toml"
    )
endif()
