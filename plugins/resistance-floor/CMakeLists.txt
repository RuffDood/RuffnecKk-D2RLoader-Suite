if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.28)
    project(RuffnecKkResistanceFloor VERSION 1.0.2 LANGUAGES CXX RC ASM_MASM)
    include(CTest)
    option(RUFFNECKK_SUITE_WARNINGS_AS_ERRORS "Treat warnings as errors" OFF)
    add_subdirectory(../../third_party/PluginSDK "${CMAKE_BINARY_DIR}/_deps/PluginSDK")
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

add_library(ruffneckk_resistance_floor SHARED
    src/floor_relays.asm
    src/plugin.cpp
    src/plugin.rc
)
target_compile_features(ruffneckk_resistance_floor PRIVATE cxx_std_20)
target_compile_definitions(ruffneckk_resistance_floor PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE
)
target_link_libraries(ruffneckk_resistance_floor PRIVATE
    D2RLPlugin::D2RLPlugin
    tomlplusplus::tomlplusplus
)
set_target_properties(ruffneckk_resistance_floor PROPERTIES
    OUTPUT_NAME "d2rl-ruffneckk-resistance-floor"
    PREFIX ""
    RUFFNECKK_PUBLIC_ARCHIVE_ELIGIBLE TRUE
)
d2rlplugin_embed_config(
    TARGET ruffneckk_resistance_floor
    FILE "${CMAKE_CURRENT_SOURCE_DIR}/config/ruffneckk-resistance-floor.toml"
)
if(MSVC)
    target_compile_options(ruffneckk_resistance_floor PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:/W4>
        $<$<COMPILE_LANGUAGE:CXX>:/permissive->
        $<$<COMPILE_LANGUAGE:CXX>:/EHsc>
        $<$<COMPILE_LANGUAGE:CXX>:/utf-8>
        $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<BOOL:${RUFFNECKK_SUITE_WARNINGS_AS_ERRORS}>>:/WX>
    )
    target_link_options(ruffneckk_resistance_floor PRIVATE /INCREMENTAL:NO /Brepro)
endif()

if(BUILD_TESTING)
    add_executable(ruffneckk_resistance_floor_tests src/policy_tests.cpp)
    target_compile_features(ruffneckk_resistance_floor_tests PRIVATE cxx_std_20)
    target_link_libraries(ruffneckk_resistance_floor_tests PRIVATE tomlplusplus::tomlplusplus)
    target_compile_definitions(ruffneckk_resistance_floor_tests PRIVATE
        RESISTANCE_FLOOR_PLUGIN_SOURCE="${CMAKE_CURRENT_SOURCE_DIR}/src/plugin.cpp"
        RESISTANCE_FLOOR_RELAY_SOURCE="${CMAKE_CURRENT_SOURCE_DIR}/src/floor_relays.asm"
        RESISTANCE_FLOOR_TOML_SOURCE="${CMAKE_CURRENT_SOURCE_DIR}/config/ruffneckk-resistance-floor.toml"
        RESISTANCE_FLOOR_CMAKE_SOURCE="${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt"
        RESISTANCE_FLOOR_RC_SOURCE="${CMAKE_CURRENT_SOURCE_DIR}/src/plugin.rc"
        RESISTANCE_FLOOR_README_SOURCE="${CMAKE_CURRENT_SOURCE_DIR}/README.md"
        RESISTANCE_FLOOR_VALIDATION_SOURCE="${CMAKE_CURRENT_SOURCE_DIR}/VALIDATION.md"
    )
    if(MSVC)
        target_compile_options(ruffneckk_resistance_floor_tests PRIVATE
            /W4 /permissive- /EHsc /utf-8
            $<$<BOOL:${RUFFNECKK_SUITE_WARNINGS_AS_ERRORS}>:/WX>
        )
    endif()
    add_test(NAME ruffneckk.resistance-floor.policy COMMAND ruffneckk_resistance_floor_tests)
endif()
