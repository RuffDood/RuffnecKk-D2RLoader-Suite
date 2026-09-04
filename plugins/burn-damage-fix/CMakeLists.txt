if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.28)
    project(RuffnecKkBurnDamageFix VERSION 1.0.1 LANGUAGES CXX RC ASM_MASM)
    include(CTest)
    option(
        RUFFNECKK_SUITE_WARNINGS_AS_ERRORS
        "Treat warnings from RuffnecKk Suite targets as errors"
        OFF
    )
    add_subdirectory(../../third_party/PluginSDK "${CMAKE_BINARY_DIR}/_deps/PluginSDK")
    add_subdirectory(../../common "${CMAKE_BINARY_DIR}/_deps/common")
else()
    enable_language(RC)
    enable_language(ASM_MASM)
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

set(BURN_DAMAGE_FIX_CONFIG
    "${CMAKE_CURRENT_SOURCE_DIR}/config/ruffneckk-burn-damage-fix.toml"
)

add_library(ruffneckk_burn_damage_fix SHARED
    src/burn_damage_fix_relay.asm
    src/plugin.cpp
    src/plugin.rc
)
target_compile_features(ruffneckk_burn_damage_fix PRIVATE cxx_std_20)
target_link_libraries(ruffneckk_burn_damage_fix PRIVATE
    RuffnecKk::Common
    tomlplusplus::tomlplusplus
)
set_target_properties(ruffneckk_burn_damage_fix PROPERTIES
    OUTPUT_NAME "d2rl-ruffneckk-burn-damage-fix"
    PREFIX ""
    RUFFNECKK_PUBLIC_ARCHIVE_ELIGIBLE TRUE
)

d2rlplugin_embed_config(
    TARGET ruffneckk_burn_damage_fix
    FILE "${BURN_DAMAGE_FIX_CONFIG}"
)

if(MSVC)
    target_compile_options(ruffneckk_burn_damage_fix PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:/EHsc>
        $<$<COMPILE_LANGUAGE:CXX>:/utf-8>
    )
    target_link_options(ruffneckk_burn_damage_fix PRIVATE /INCREMENTAL:NO)
endif()

if(BUILD_TESTING)
    add_executable(ruffneckk_burn_damage_fix_tests
        tests/policy_tests.cpp
    )
    target_compile_features(ruffneckk_burn_damage_fix_tests PRIVATE cxx_std_20)
    target_include_directories(ruffneckk_burn_damage_fix_tests PRIVATE src)
    target_compile_definitions(ruffneckk_burn_damage_fix_tests PRIVATE
        BURN_DAMAGE_FIX_PLUGIN_SOURCE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/src/plugin.cpp"
    )
    target_link_libraries(ruffneckk_burn_damage_fix_tests PRIVATE
        tomlplusplus::tomlplusplus
    )
    if(MSVC)
        target_compile_options(ruffneckk_burn_damage_fix_tests PRIVATE /EHsc /utf-8)
    endif()
    add_test(
        NAME ruffneckk.burn-damage-fix.policy
        COMMAND ruffneckk_burn_damage_fix_tests
    )
endif()
