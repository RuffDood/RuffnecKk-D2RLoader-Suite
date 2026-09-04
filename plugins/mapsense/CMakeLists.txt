if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.28)
    project(RuffnecKkMapSense VERSION 1.0.2 LANGUAGES C CXX RC)
    include(CTest)
    option(RUFFNECKK_SUITE_WARNINGS_AS_ERRORS "Treat warnings as errors" OFF)
    add_subdirectory(../../third_party/PluginSDK "${CMAKE_BINARY_DIR}/_deps/PluginSDK")
else()
    enable_language(C)
    enable_language(RC)
endif()

include(FetchContent)
set(TOMLPLUSPLUS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(TOMLPLUSPLUS_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(tomlplusplus
    GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
    GIT_TAG v3.4.0
    GIT_SHALLOW TRUE
)
FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    GIT_SHALLOW TRUE
)
FetchContent_Declare(minhook
    GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
    GIT_TAG c3fcafdc10146beb5919319d0683e44e3c30d537
)
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG f401021d5a5d56fe2304056c391e78f81c8d4b8f
)
FetchContent_MakeAvailable(tomlplusplus nlohmann_json minhook imgui)

add_library(ruffneckk_mapsense_imgui STATIC
    "${imgui_SOURCE_DIR}/imgui.cpp"
    "${imgui_SOURCE_DIR}/imgui_draw.cpp"
    "${imgui_SOURCE_DIR}/imgui_tables.cpp"
    "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_dx12.cpp"
)
target_include_directories(ruffneckk_mapsense_imgui PUBLIC
    "${imgui_SOURCE_DIR}"
    "${imgui_SOURCE_DIR}/backends"
)
target_compile_features(ruffneckk_mapsense_imgui PUBLIC cxx_std_20)
target_compile_definitions(ruffneckk_mapsense_imgui PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE
)
target_link_libraries(ruffneckk_mapsense_imgui PUBLIC dxgi)
if(MSVC)
    target_compile_options(ruffneckk_mapsense_imgui PRIVATE /W3 /WX-)
endif()

add_library(ruffneckk_mapsense SHARED
    src/d3d12_imgui_host.cpp
    src/external_atlas_cache.cpp
    src/external_atlas_geometry.cpp
    src/automap_sprite_package.cpp
    src/external_label_provider.cpp
    src/imgui_settings_panel.cpp
    src/mapsense_data_catalog.cpp
    src/navigation_engine.cpp
    src/navigation_level_catalog.cpp
    src/navigation_policy.cpp
    src/navigation_resolver.cpp
    src/native_automap_marker.cpp
    src/native_automap_missile.cpp
    src/native_automap_poi.cpp
    src/native_ui_state.cpp
    src/overlay_scene.cpp
    src/plugin.cpp
    src/reveal_engine.cpp
    src/sfilllocation_diagnostic.cpp
    src/ui_localization.cpp
    src/plugin.rc
)
target_compile_features(ruffneckk_mapsense PRIVATE cxx_std_20)
target_compile_definitions(ruffneckk_mapsense PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE
)
target_link_libraries(ruffneckk_mapsense PRIVATE
    D2RLPlugin::D2RLPlugin
    ruffneckk_mapsense_imgui
    minhook
    tomlplusplus::tomlplusplus
    dxgi comctl32 shell32 windowscodecs ole32 crypt32
)
set_target_properties(ruffneckk_mapsense PROPERTIES
    OUTPUT_NAME "d2rl-ruffneckk-mapsense"
    PREFIX ""
    RUFFNECKK_PUBLIC_ARCHIVE_ELIGIBLE TRUE
)
d2rlplugin_embed_config(
    TARGET ruffneckk_mapsense
    FILE "${CMAKE_CURRENT_SOURCE_DIR}/config/ruffneckk-mapsense.toml"
)

set(RUFFNECKK_MAPSENSE_MAPGEN_EXECUTABLE "" CACHE FILEPATH
    "Optional prebuilt MapSense map generator copied beside the plugin DLL"
)
if(RUFFNECKK_MAPSENSE_MAPGEN_EXECUTABLE)
    add_custom_command(TARGET ruffneckk_mapsense POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${RUFFNECKK_MAPSENSE_MAPGEN_EXECUTABLE}"
            "$<TARGET_FILE_DIR:ruffneckk_mapsense>/RuffnecKkMapSenseMapgen.exe"
        VERBATIM
    )
endif()

if(MSVC)
    target_compile_options(ruffneckk_mapsense PRIVATE
        /W4 /permissive- /EHsc /utf-8
        $<$<BOOL:${RUFFNECKK_SUITE_WARNINGS_AS_ERRORS}>:/WX>
    )
    target_link_options(ruffneckk_mapsense PRIVATE /Brepro)
endif()

if(BUILD_TESTING)
    add_executable(ruffneckk_mapsense_tests
        src/external_atlas_cache.cpp
        src/external_atlas_geometry.cpp
        src/automap_sprite_package.cpp
        src/mapsense_data_catalog.cpp
        src/mapsense_tests.cpp
        src/navigation_engine.cpp
        src/navigation_level_catalog.cpp
        src/navigation_policy.cpp
        src/overlay_scene.cpp
        src/ui_localization.cpp
    )
    target_compile_features(ruffneckk_mapsense_tests PRIVATE cxx_std_20)
    target_compile_definitions(ruffneckk_mapsense_tests PRIVATE
        WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE
    )
    target_link_libraries(ruffneckk_mapsense_tests PRIVATE
        D2RLPlugin::D2RLPlugin
        tomlplusplus::tomlplusplus
        nlohmann_json::nlohmann_json
    )
    if(MSVC)
        target_compile_options(ruffneckk_mapsense_tests PRIVATE
            /W4 /permissive- /EHsc /utf-8
            $<$<BOOL:${RUFFNECKK_SUITE_WARNINGS_AS_ERRORS}>:/WX>
        )
    endif()
    add_test(NAME ruffneckk.mapsense.policy
        COMMAND ruffneckk_mapsense_tests
            "${CMAKE_CURRENT_SOURCE_DIR}/config/ruffneckk-mapsense.toml"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/RuffnecKkMapSenseAutomapSprites.msp"
    )
endif()
