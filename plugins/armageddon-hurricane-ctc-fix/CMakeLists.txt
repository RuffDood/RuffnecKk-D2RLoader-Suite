if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.28)
    project(RuffnecKkArmageddonHurricaneCtCFix VERSION 1.0.0 LANGUAGES CXX RC)
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

add_library(ruffneckk_armageddon_hurricane_ctc_fix SHARED
    src/plugin.cpp
    src/plugin.rc
)
target_compile_features(ruffneckk_armageddon_hurricane_ctc_fix PRIVATE cxx_std_20)
target_link_libraries(ruffneckk_armageddon_hurricane_ctc_fix PRIVATE
    D2RLPlugin::D2RLPlugin
    tomlplusplus::tomlplusplus
)
set_target_properties(ruffneckk_armageddon_hurricane_ctc_fix PROPERTIES
    OUTPUT_NAME "d2rl-ruffneckk-armageddon-hurricane-ctc-fix"
    PREFIX ""
    RUFFNECKK_PUBLIC_ARCHIVE_ELIGIBLE TRUE
)
d2rlplugin_embed_config(
    TARGET ruffneckk_armageddon_hurricane_ctc_fix
    FILE "${CMAKE_CURRENT_SOURCE_DIR}/config/ruffneckk-armageddon-hurricane-ctc-fix.toml"
)
if(MSVC)
    target_compile_options(ruffneckk_armageddon_hurricane_ctc_fix PRIVATE
        /utf-8 /W4 /permissive-
        $<$<BOOL:${RUFFNECKK_SUITE_WARNINGS_AS_ERRORS}>:/WX>
    )
    target_link_options(ruffneckk_armageddon_hurricane_ctc_fix PRIVATE /INCREMENTAL:NO /Brepro)
endif()

if(BUILD_TESTING)
    add_executable(ruffneckk_armageddon_hurricane_ctc_fix_tests src/policy_tests.cpp)
    target_compile_features(ruffneckk_armageddon_hurricane_ctc_fix_tests PRIVATE cxx_std_20)
    target_link_libraries(ruffneckk_armageddon_hurricane_ctc_fix_tests PRIVATE tomlplusplus::tomlplusplus)
    target_compile_definitions(ruffneckk_armageddon_hurricane_ctc_fix_tests PRIVATE
        ARMAGEDDON_CTC_CONFIG_FILE="${CMAKE_CURRENT_SOURCE_DIR}/config/ruffneckk-armageddon-hurricane-ctc-fix.toml"
        ARMAGEDDON_CTC_PLUGIN_FILE="${CMAKE_CURRENT_SOURCE_DIR}/src/plugin.cpp"
    )
    if(MSVC)
        target_compile_options(ruffneckk_armageddon_hurricane_ctc_fix_tests PRIVATE
            /utf-8 /W4 /permissive-
            $<$<BOOL:${RUFFNECKK_SUITE_WARNINGS_AS_ERRORS}>:/WX>
        )
    endif()
    add_test(NAME ruffneckk.armageddon-hurricane-ctc-fix.policy COMMAND ruffneckk_armageddon_hurricane_ctc_fix_tests)
endif()
