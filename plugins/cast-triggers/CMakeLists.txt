if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.28)
    project(RuffnecKkCastTriggers VERSION 1.0.0 LANGUAGES CXX RC)
    include(CTest)
    option(RUFFNECKK_SUITE_WARNINGS_AS_ERRORS "Treat warnings as errors" OFF)
    add_subdirectory(../../third_party/PluginSDK "${CMAKE_BINARY_DIR}/_deps/PluginSDK")
else()
    enable_language(RC)
endif()

include(FetchContent)
if(NOT TARGET tomlplusplus::tomlplusplus)
    set(TOMLPLUSPLUS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(TOMLPLUSPLUS_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        tomlplusplus
        GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
        GIT_TAG v3.4.0
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(tomlplusplus)
endif()

add_library(ruffneckk_cast_triggers SHARED src/plugin.cpp src/plugin.rc)
target_compile_features(ruffneckk_cast_triggers PRIVATE cxx_std_20)
target_link_libraries(ruffneckk_cast_triggers PRIVATE
    D2RLPlugin::D2RLPlugin
    tomlplusplus::tomlplusplus
)
set_target_properties(ruffneckk_cast_triggers PROPERTIES
    OUTPUT_NAME "d2rl-ruffneckk-cast-triggers"
    PREFIX ""
    RUFFNECKK_PUBLIC_ARCHIVE_ELIGIBLE TRUE
)
d2rlplugin_embed_config(
    TARGET ruffneckk_cast_triggers
    FILE "${CMAKE_CURRENT_SOURCE_DIR}/config/ruffneckk-cast-triggers.toml"
)
if(MSVC)
    target_compile_options(ruffneckk_cast_triggers PRIVATE
        /utf-8 /W4 /permissive-
        $<$<BOOL:${RUFFNECKK_SUITE_WARNINGS_AS_ERRORS}>:/WX>
    )
    target_link_options(ruffneckk_cast_triggers PRIVATE /INCREMENTAL:NO /Brepro)
endif()

if(BUILD_TESTING)
    add_executable(ruffneckk_cast_triggers_tests src/policy_tests.cpp)
    target_compile_features(ruffneckk_cast_triggers_tests PRIVATE cxx_std_20)
    target_link_libraries(ruffneckk_cast_triggers_tests PRIVATE tomlplusplus::tomlplusplus)
    target_compile_definitions(ruffneckk_cast_triggers_tests PRIVATE
        CAST_TRIGGERS_CONFIG_FILE="${CMAKE_CURRENT_SOURCE_DIR}/config/ruffneckk-cast-triggers.toml"
        CAST_TRIGGERS_PLUGIN_FILE="${CMAKE_CURRENT_SOURCE_DIR}/src/plugin.cpp"
    )
    if(MSVC)
        target_compile_options(ruffneckk_cast_triggers_tests PRIVATE
            /utf-8 /W4 /permissive-
            $<$<BOOL:${RUFFNECKK_SUITE_WARNINGS_AS_ERRORS}>:/WX>
        )
    endif()
    add_test(NAME ruffneckk.cast-triggers.policy COMMAND ruffneckk_cast_triggers_tests)
endif()
