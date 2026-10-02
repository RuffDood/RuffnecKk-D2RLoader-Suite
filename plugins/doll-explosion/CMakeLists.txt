if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.28)
    project(DollExplosion VERSION 1.0.0 LANGUAGES CXX RC)
    include(CTest)
else()
    enable_language(RC)
endif()

include(FetchContent)

# The Suite vendors PluginSDK v3 at 4933e2c42cb2592958cd0df3b6dc5003102252d1.
if(NOT TARGET D2RLPlugin::D2RLPlugin)
    add_subdirectory(../../third_party/PluginSDK "${CMAKE_BINARY_DIR}/_deps/PluginSDK")
endif()
if(NOT TARGET D2RLPluginV4::D2RLPluginV4)
    add_subdirectory(../../third_party/PluginSDK-v4 "${CMAKE_BINARY_DIR}/_deps/PluginSDK-v4")
endif()
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

# Use the Suite-owned adapter for native stat-provider admission.
if(NOT TARGET RuffnecKk::NativeStatCompat)
    add_subdirectory(../../common "${CMAKE_BINARY_DIR}/_deps/common")
endif()
add_library(DollExplosion SHARED
    src/plugin.cpp
    src/plugin.rc
)
target_compile_features(DollExplosion PRIVATE cxx_std_20)
target_compile_definitions(DollExplosion PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    UNICODE
    _UNICODE
)
target_link_libraries(DollExplosion PRIVATE
    D2RLPlugin::D2RLPlugin
    RuffnecKk::NativeStatCompat
    tomlplusplus::tomlplusplus
)
set_target_properties(DollExplosion PROPERTIES
    OUTPUT_NAME "DollExplosion"
    PREFIX ""
    RUFFNECKK_PUBLIC_ARCHIVE_ELIGIBLE FALSE
)
d2rlplugin_embed_config(
    TARGET DollExplosion
    FILE "${CMAKE_CURRENT_SOURCE_DIR}/config/ruffneckk-doll-explosion.toml"
)

if(MSVC)
    target_compile_options(DollExplosion PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:/W4>
        $<$<COMPILE_LANGUAGE:CXX>:/WX>
        $<$<COMPILE_LANGUAGE:CXX>:/permissive->
        $<$<COMPILE_LANGUAGE:CXX>:/EHsc>
        $<$<COMPILE_LANGUAGE:CXX>:/utf-8>
    )
    target_link_options(DollExplosion PRIVATE /INCREMENTAL:NO /Brepro)
endif()

if(BUILD_TESTING)
    add_executable(DollExplosionDelayedOwnerTests src/delayed_owner_tests.cpp)
    target_compile_features(DollExplosionDelayedOwnerTests PRIVATE cxx_std_20)
    target_compile_definitions(DollExplosionDelayedOwnerTests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
    target_link_libraries(DollExplosionDelayedOwnerTests PRIVATE D2RLPlugin::D2RLPlugin RuffnecKk::NativeStatCompat tomlplusplus::tomlplusplus)
    if(MSVC)
        target_compile_options(DollExplosionDelayedOwnerTests PRIVATE /W4 /WX /permissive- /EHsc /utf-8)
    endif()
    add_test(NAME doll-explosion-delayed-owner COMMAND DollExplosionDelayedOwnerTests)
    add_executable(DollExplosionTests src/policy_tests.cpp)
    target_compile_features(DollExplosionTests PRIVATE cxx_std_20)
    target_link_libraries(DollExplosionTests PRIVATE
        RuffnecKk::NativeStatCompat
        tomlplusplus::tomlplusplus
    )
    target_compile_definitions(DollExplosionTests PRIVATE
        DOLL_EXPLOSION_PLUGIN_SOURCE="${CMAKE_CURRENT_SOURCE_DIR}/src/plugin.cpp"
        DOLL_EXPLOSION_NATIVE_SOURCE="${CMAKE_CURRENT_SOURCE_DIR}/src/native_fingerprint.hpp"
        DOLL_EXPLOSION_CMAKE_SOURCE="${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt"
        DOLL_EXPLOSION_TOML_SOURCE="${CMAKE_CURRENT_SOURCE_DIR}/config/ruffneckk-doll-explosion.toml"
    )
    if(MSVC)
        target_compile_options(DollExplosionTests PRIVATE
            /W4 /WX /permissive- /EHsc /utf-8
        )
    endif()
    add_test(NAME doll-explosion-policy COMMAND DollExplosionTests)
    set(DOLL_EXPLOSION_NATIVE_IMAGE "" CACHE FILEPATH "Optional governed native PE for witness validation")
    if(DOLL_EXPLOSION_NATIVE_IMAGE)
        add_test(NAME doll-explosion-native-image
            COMMAND DollExplosionTests "${DOLL_EXPLOSION_NATIVE_IMAGE}")
    endif()
endif()
