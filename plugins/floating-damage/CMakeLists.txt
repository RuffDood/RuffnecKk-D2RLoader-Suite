if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.28)
    project(RuffnecKkFloatingDamage LANGUAGES C CXX RC)
    include(CTest)
    option(
        RUFFNECKK_SUITE_WARNINGS_AS_ERRORS
        "Treat warnings from RuffnecKk Suite targets as errors"
        OFF
    )
    add_subdirectory(../../third_party/PluginSDK "${CMAKE_BINARY_DIR}/_deps/PluginSDK")
    add_subdirectory(../../common "${CMAKE_BINARY_DIR}/_deps/common")
else()
    enable_language(C)
    enable_language(RC)
endif()

include(FetchContent)

if(NOT TARGET minhook)
    FetchContent_Declare(
        minhook
        GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
        GIT_TAG c3fcafdc10146beb5919319d0683e44e3c30d537
    )
    FetchContent_MakeAvailable(minhook)
endif()

FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG f401021d5a5d56fe2304056c391e78f81c8d4b8f
)
FetchContent_MakeAvailable(imgui)

add_library(ruffneckk_floating_damage_imgui STATIC
    "${imgui_SOURCE_DIR}/imgui.cpp"
    "${imgui_SOURCE_DIR}/imgui_draw.cpp"
    "${imgui_SOURCE_DIR}/imgui_tables.cpp"
    "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_dx12.cpp"
)
target_include_directories(ruffneckk_floating_damage_imgui PUBLIC
    "${imgui_SOURCE_DIR}"
    "${imgui_SOURCE_DIR}/backends"
)
target_compile_features(ruffneckk_floating_damage_imgui PUBLIC cxx_std_20)
target_compile_definitions(ruffneckk_floating_damage_imgui PRIVATE
    NOMINMAX
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
)
target_link_libraries(ruffneckk_floating_damage_imgui PUBLIC dxgi)
if(MSVC)
    target_compile_options(ruffneckk_floating_damage_imgui PRIVATE /W3 /WX-)
endif()

set(FLOATING_DAMAGE_CONFIG
    "${CMAKE_CURRENT_SOURCE_DIR}/config/ruffneckk-floating-damage.toml"
)
file(READ "${FLOATING_DAMAGE_CONFIG}" FLOATING_DAMAGE_DEFAULT_TOML)
set(FLOATING_DAMAGE_GENERATED_DIR
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
)
file(MAKE_DIRECTORY "${FLOATING_DAMAGE_GENERATED_DIR}")
configure_file(
    src/default_config.hpp.in
    "${FLOATING_DAMAGE_GENERATED_DIR}/default_config.hpp"
    @ONLY
)

add_library(ruffneckk_floating_damage SHARED
    src/config_parser.cpp
    src/d3d12_renderer.cpp
    src/floating_damage.cpp
    src/plugin.cpp
    src/plugin.rc
)
target_link_libraries(ruffneckk_floating_damage PRIVATE
    RuffnecKk::Common
    ruffneckk_floating_damage_imgui
    minhook
    dxgi
)
target_compile_features(ruffneckk_floating_damage PRIVATE cxx_std_20)
target_include_directories(ruffneckk_floating_damage PRIVATE
    src
    "${FLOATING_DAMAGE_GENERATED_DIR}"
)
if(MSVC)
    target_compile_options(ruffneckk_floating_damage PRIVATE /EHsc)
endif()
set_target_properties(ruffneckk_floating_damage PROPERTIES
    OUTPUT_NAME "d2rl-ruffneckk-floating-damage"
    PREFIX ""
    RUFFNECKK_PUBLIC_ARCHIVE_ELIGIBLE TRUE
)

d2rlplugin_embed_config(
    TARGET ruffneckk_floating_damage
    FILE "${FLOATING_DAMAGE_CONFIG}"
)

if(BUILD_TESTING)
    add_executable(ruffneckk_floating_damage_tests
        tests/config_tests.cpp
        src/config_parser.cpp
    )
    target_link_libraries(ruffneckk_floating_damage_tests PRIVATE
        RuffnecKk::Common
        ruffneckk_floating_damage_imgui
    )
    target_compile_features(ruffneckk_floating_damage_tests PRIVATE cxx_std_20)
    target_include_directories(ruffneckk_floating_damage_tests PRIVATE
        src
        "${FLOATING_DAMAGE_GENERATED_DIR}"
    )
    if(MSVC)
        target_compile_options(ruffneckk_floating_damage_tests PRIVATE /EHsc)
    endif()
    add_test(
        NAME ruffneckk.floating-damage.config
        COMMAND ruffneckk_floating_damage_tests "${FLOATING_DAMAGE_CONFIG}"
    )

    if(MSVC)
        get_filename_component(FLOATING_DAMAGE_MSVC_BIN_DIR
            "${CMAKE_LINKER}" DIRECTORY
        )
        find_program(FLOATING_DAMAGE_DUMPBIN_EXECUTABLE
            NAMES dumpbin
            HINTS "${FLOATING_DAMAGE_MSVC_BIN_DIR}"
            REQUIRED
        )
        find_program(FLOATING_DAMAGE_POWERSHELL_EXECUTABLE
            NAMES pwsh powershell
            REQUIRED
        )
        add_test(
            NAME ruffneckk.floating-damage.binary-policy
            COMMAND
                "${FLOATING_DAMAGE_POWERSHELL_EXECUTABLE}"
                -NoProfile
                -ExecutionPolicy Bypass
                -File
                    "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/Test-Suite.ps1"
                -FloatingDamageDll "$<TARGET_FILE:ruffneckk_floating_damage>"
                -DumpbinPath "${FLOATING_DAMAGE_DUMPBIN_EXECUTABLE}"
        )
        add_test(
            NAME ruffneckk.floating-damage.source-policy
            COMMAND
                "${FLOATING_DAMAGE_POWERSHELL_EXECUTABLE}"
                -NoProfile
                -ExecutionPolicy Bypass
                -File
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/Test-FloatingDamageSourcePolicy.ps1"
                -PluginDirectory "${CMAKE_CURRENT_SOURCE_DIR}"
        )
    endif()
endif()
