if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.28)
    project(RuffnecKkExtendedActLevelIds VERSION 1.0.0 LANGUAGES CXX RC)
    include(CTest)
    option(RUFFNECKK_SUITE_WARNINGS_AS_ERRORS "Treat warnings as errors" OFF)
    add_subdirectory(../../third_party/PluginSDK "${CMAKE_BINARY_DIR}/_deps/PluginSDK")
else()
    enable_language(RC)
endif()

add_library(ruffneckk_extended_act_level_ids SHARED
    src/plugin.cpp
    src/plugin.rc
)
target_compile_features(ruffneckk_extended_act_level_ids PRIVATE cxx_std_20)
target_link_libraries(ruffneckk_extended_act_level_ids PRIVATE D2RLPlugin::D2RLPlugin)
set_target_properties(ruffneckk_extended_act_level_ids PROPERTIES
    OUTPUT_NAME "d2rl-ruffneckk-extended-act-level-ids"
    PREFIX ""
    RUFFNECKK_PUBLIC_ARCHIVE_ELIGIBLE TRUE
)
if(MSVC)
    target_compile_options(ruffneckk_extended_act_level_ids PRIVATE
        /utf-8 /W4 /permissive-
        $<$<BOOL:${RUFFNECKK_SUITE_WARNINGS_AS_ERRORS}>:/WX>
    )
    target_link_options(ruffneckk_extended_act_level_ids PRIVATE /INCREMENTAL:NO /Brepro)
endif()

if(BUILD_TESTING)
    add_executable(ruffneckk_extended_act_level_ids_tests src/policy_tests.cpp)
    target_compile_features(ruffneckk_extended_act_level_ids_tests PRIVATE cxx_std_20)
    target_compile_definitions(ruffneckk_extended_act_level_ids_tests PRIVATE
        EXTENDED_ACT_PLUGIN_FILE="${CMAKE_CURRENT_SOURCE_DIR}/src/plugin.cpp"
    )
    if(MSVC)
        target_compile_options(ruffneckk_extended_act_level_ids_tests PRIVATE
            /utf-8 /W4 /permissive-
            $<$<BOOL:${RUFFNECKK_SUITE_WARNINGS_AS_ERRORS}>:/WX>
        )
    endif()
    add_test(NAME ruffneckk.extended-act-level-ids.policy COMMAND ruffneckk_extended_act_level_ids_tests)
endif()
