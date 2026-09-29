if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.28)
    project(RuffnecKkCastTriggers VERSION 1.1.12 LANGUAGES CXX RC)
    include(CTest)
    option(RUFFNECKK_SUITE_WARNINGS_AS_ERRORS "Treat warnings as errors" OFF)
    add_subdirectory(../../third_party/PluginSDK "${CMAKE_BINARY_DIR}/_deps/PluginSDK")
    add_subdirectory(../../common "${CMAKE_BINARY_DIR}/_deps/common")
else()
    enable_language(RC)
endif()

include(FetchContent)
option(RUFFNECKK_CAST_TRIGGERS_DAMAGE_CLEANUP_V1
    "Build the isolated 1.0.2 damage-cleanup candidate (not release eligible)" OFF)
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

enable_language(ASM_MASM)
add_library(ruffneckk_cast_triggers SHARED src/plugin.cpp src/proc_presentation.cpp src/plugin.rc src/caster_body.cpp src/caster_body.asm src/damage_body.cpp src/damage_body.asm)
target_compile_features(ruffneckk_cast_triggers PRIVATE cxx_std_20)
target_link_libraries(ruffneckk_cast_triggers PRIVATE
    D2RLPlugin::D2RLPlugin
    RuffnecKk::NativeStatCompat
    tomlplusplus::tomlplusplus
)
set_target_properties(ruffneckk_cast_triggers PROPERTIES
    OUTPUT_NAME "d2rl-ruffneckk-cast-triggers"
    PREFIX ""
    RUFFNECKK_PUBLIC_ARCHIVE_ELIGIBLE FALSE
)
if(RUFFNECKK_CAST_TRIGGERS_DAMAGE_CLEANUP_V1)
    target_compile_definitions(ruffneckk_cast_triggers PRIVATE
        RUFFNECKK_DAMAGE_CLEANUP_V1=1)
    set_target_properties(ruffneckk_cast_triggers PROPERTIES
        RUFFNECKK_PUBLIC_ARCHIVE_ELIGIBLE FALSE)
endif()
d2rlplugin_embed_config(
    TARGET ruffneckk_cast_triggers
    FILE "${CMAKE_CURRENT_SOURCE_DIR}/config/ruffneckk-cast-triggers.toml"
)
if(MSVC)
    target_compile_options(ruffneckk_cast_triggers PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:/utf-8;/W4;/permissive->
        $<$<BOOL:${RUFFNECKK_SUITE_WARNINGS_AS_ERRORS}>:/WX>
    )
    target_link_options(ruffneckk_cast_triggers PRIVATE /INCREMENTAL:NO /Brepro)
endif()

if(BUILD_TESTING)
    add_executable(ruffneckk_damage_cleanup_tests src/damage_cleanup_tests.cpp)
    target_compile_features(ruffneckk_damage_cleanup_tests PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(ruffneckk_damage_cleanup_tests PRIVATE /utf-8 /W4 /WX /EHsc)
    endif()
    add_test(NAME ruffneckk.cast-triggers.damage-cleanup
        COMMAND ruffneckk_damage_cleanup_tests)
    set_tests_properties(ruffneckk.cast-triggers.damage-cleanup PROPERTIES TIMEOUT 20)
    if(RUFFNECKK_CAST_TRIGGERS_DAMAGE_CLEANUP_V1)
        add_test(NAME ruffneckk.cast-triggers.damage-cleanup-binary
            COMMAND ruffneckk_damage_cleanup_tests $<TARGET_FILE:ruffneckk_cast_triggers>)
        set_tests_properties(ruffneckk.cast-triggers.damage-cleanup-binary PROPERTIES TIMEOUT 20)
    endif()
    add_executable(ruffneckk_cast_triggers_tests src/policy_tests.cpp)
    target_compile_features(ruffneckk_cast_triggers_tests PRIVATE cxx_std_20)
    target_link_libraries(ruffneckk_cast_triggers_tests PRIVATE tomlplusplus::tomlplusplus)
    target_compile_definitions(ruffneckk_cast_triggers_tests PRIVATE
        CAST_TRIGGERS_CONFIG_FILE="${CMAKE_CURRENT_SOURCE_DIR}/config/ruffneckk-cast-triggers.toml"
        CAST_TRIGGERS_PLUGIN_FILE="${CMAKE_CURRENT_SOURCE_DIR}/src/plugin.cpp"
    )
    if(MSVC)
        target_compile_options(ruffneckk_cast_triggers_tests PRIVATE
            $<$<COMPILE_LANGUAGE:CXX>:/utf-8;/W4;/permissive->
            $<$<BOOL:${RUFFNECKK_SUITE_WARNINGS_AS_ERRORS}>:/WX>
        )
    endif()
    add_test(NAME ruffneckk.cast-triggers.policy COMMAND ruffneckk_cast_triggers_tests)
endif()

# Regression checks retained from the qualified compatibility candidate.
if(BUILD_TESTING)
    foreach(check callsite wide_event teleport)
        add_executable(ruffneckk_cast_${check}_tests src/${check}_tests.cpp)
        target_compile_features(ruffneckk_cast_${check}_tests PRIVATE cxx_std_20)
        if(MSVC)
            target_compile_options(ruffneckk_cast_${check}_tests PRIVATE /utf-8 /W4 /WX /EHsc)
        endif()
        add_test(NAME ruffneckk.cast-triggers.${check} COMMAND ruffneckk_cast_${check}_tests)
    endforeach()
    foreach(check caster_body damage_body proc_presentation)
        add_executable(ruffneckk_cast_${check}_tests src/${check}_tests.cpp)
        target_compile_features(ruffneckk_cast_${check}_tests PRIVATE cxx_std_20)
        if(MSVC)
            target_compile_options(ruffneckk_cast_${check}_tests PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/utf-8;/W4;/WX;/EHsc>)
        endif()
        add_test(NAME ruffneckk.cast-triggers.${check} COMMAND ruffneckk_cast_${check}_tests)
    endforeach()
    target_sources(ruffneckk_cast_caster_body_tests PRIVATE src/caster_body.cpp src/caster_body.asm)
    target_sources(ruffneckk_cast_damage_body_tests PRIVATE src/damage_body.cpp src/damage_body.asm src/damage_body_test_probe.asm)
    target_sources(ruffneckk_cast_proc_presentation_tests PRIVATE src/proc_presentation_test_probe.asm)
    target_link_libraries(ruffneckk_cast_proc_presentation_tests PRIVATE D2RLPlugin::D2RLPlugin tomlplusplus::tomlplusplus)
endif()
