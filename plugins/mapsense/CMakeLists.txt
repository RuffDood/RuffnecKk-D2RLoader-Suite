if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.28)
    project(RuffnecKkMapSense VERSION 2.2.0 LANGUAGES C CXX RC)
    include(CTest)
    option(RUFFNECKK_SUITE_WARNINGS_AS_ERRORS "Treat warnings as errors" OFF)
    add_subdirectory(../../third_party/PluginSDK "${CMAKE_BINARY_DIR}/_deps/PluginSDK")
    add_subdirectory(../../common "${CMAKE_BINARY_DIR}/_deps/common")
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

option(RUFFNECKK_MAPSENSE_GPU_DIAGNOSTICS
    "Enable process-local DRED capture for a non-public MapSense diagnostic candidate" OFF)
option(RUFFNECKK_MAPSENSE_GPS_COLLISION_DIAGNOSTICS
    "Enable clone-only native CollMap capture for a non-public MapSense diagnostic candidate" OFF)
option(RUFFNECKK_MAPSENSE_GPS_ROUTE_DIAGNOSTICS
    "Enable asynchronous GPS route guidance" ON)

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
    src/d3d12_gpu_diagnostics.cpp
    src/external_atlas_cache.cpp
    src/external_atlas_geometry.cpp
    src/automap_sprite_package.cpp
    src/external_label_provider.cpp
    src/gps_collision_artifact.cpp
    src/gps_collision_probe.cpp
    src/gps_route_artifact.cpp
    $<$<BOOL:${RUFFNECKK_MAPSENSE_GPS_ROUTE_DIAGNOSTICS}>:src/gps_route_coordinator.cpp>
    $<$<BOOL:${RUFFNECKK_MAPSENSE_GPS_ROUTE_DIAGNOSTICS}>:src/gps_route_provider.cpp>
    src/imgui_settings_panel.cpp
    src/localized_fonts.cpp
    src/txt_mode.cpp
    src/mapsense_data_catalog.cpp
    src/navigation_engine.cpp
    src/navigation_level_catalog.cpp
    src/navigation_policy.cpp src/navigation_routing.cpp
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
    RUFFNECKK_MAPSENSE_GPU_DIAGNOSTICS=$<BOOL:${RUFFNECKK_MAPSENSE_GPU_DIAGNOSTICS}>
    RUFFNECKK_MAPSENSE_ENABLE_GPS_COLLISION_PROBE=$<BOOL:${RUFFNECKK_MAPSENSE_GPS_COLLISION_DIAGNOSTICS}>
    RUFFNECKK_MAPSENSE_ENABLE_GPS_ROUTE_DIAGNOSTICS=$<BOOL:${RUFFNECKK_MAPSENSE_GPS_ROUTE_DIAGNOSTICS}>
)
target_link_libraries(ruffneckk_mapsense PRIVATE
    RuffnecKk::Common
    RuffnecKk::NativeStatCompat
    ruffneckk_mapsense_imgui
    minhook
    tomlplusplus::tomlplusplus
    dxgi comctl32 shell32 windowscodecs ole32 crypt32 bcrypt
)
set_target_properties(ruffneckk_mapsense PROPERTIES
    OUTPUT_NAME "d2rl-ruffneckk-mapsense"
    PREFIX ""
    RUFFNECKK_PUBLIC_ARCHIVE_ELIGIBLE TRUE
)
if(RUFFNECKK_MAPSENSE_GPU_DIAGNOSTICS OR RUFFNECKK_MAPSENSE_GPS_COLLISION_DIAGNOSTICS)
    set_target_properties(ruffneckk_mapsense PROPERTIES RUFFNECKK_PUBLIC_ARCHIVE_ELIGIBLE FALSE)
endif()
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
    add_executable(ruffneckk_mapsense_tracked_native_transform_tests
        src/tracked_native_transform_compat_tests.cpp)
    target_compile_features(ruffneckk_mapsense_tracked_native_transform_tests PRIVATE cxx_std_20)
    target_link_libraries(ruffneckk_mapsense_tracked_native_transform_tests PRIVATE
        RuffnecKk::Common)
    if(MSVC)
        target_compile_options(ruffneckk_mapsense_tracked_native_transform_tests PRIVATE
            /W4 /permissive- /EHsc /utf-8
            $<$<BOOL:${RUFFNECKK_SUITE_WARNINGS_AS_ERRORS}>:/WX>)
    endif()
    add_test(NAME ruffneckk.mapsense.tracked-native-transform
        COMMAND ruffneckk_mapsense_tracked_native_transform_tests)

    add_executable(ruffneckk_mapsense_renderer_discovery_policy_tests
        src/d3d12_renderer_discovery_policy_tests.cpp)
    target_compile_features(ruffneckk_mapsense_renderer_discovery_policy_tests PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(ruffneckk_mapsense_renderer_discovery_policy_tests PRIVATE
            /W4 /permissive- /EHsc /utf-8
            $<$<BOOL:${RUFFNECKK_SUITE_WARNINGS_AS_ERRORS}>:/WX>)
    endif()
    add_test(NAME ruffneckk.mapsense.renderer-discovery-policy
        COMMAND ruffneckk_mapsense_renderer_discovery_policy_tests)

    add_executable(ruffneckk_mapsense_gps_follow_tests
        src/navigation_gps_follow_tests.cpp src/navigation_engine.cpp)
    target_compile_features(ruffneckk_mapsense_gps_follow_tests PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(ruffneckk_mapsense_gps_follow_tests PRIVATE
            /W4 /permissive- /EHsc /utf-8
            $<$<BOOL:${RUFFNECKK_SUITE_WARNINGS_AS_ERRORS}>:/WX>)
    endif()
    add_test(NAME ruffneckk.mapsense.gps-follow
        COMMAND ruffneckk_mapsense_gps_follow_tests)

    add_executable(ruffneckk_mapsense_gps_coordinator_tests
        src/gps_route_coordinator_tests.cpp
        src/gps_route_coordinator.cpp
        src/navigation_engine.cpp
        src/navigation_policy.cpp src/navigation_routing.cpp)
    target_compile_features(ruffneckk_mapsense_gps_coordinator_tests PRIVATE cxx_std_20)
    target_compile_definitions(ruffneckk_mapsense_gps_coordinator_tests PRIVATE
        WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
    if(MSVC)
        target_compile_options(ruffneckk_mapsense_gps_coordinator_tests PRIVATE
            /W4 /permissive- /EHsc /utf-8
            $<$<BOOL:${RUFFNECKK_SUITE_WARNINGS_AS_ERRORS}>:/WX>)
    endif()
    add_test(NAME ruffneckk.mapsense.gps-coordinator
        COMMAND ruffneckk_mapsense_gps_coordinator_tests)

    add_executable(ruffneckk_mapsense_gps_collision_probe_tests
        src/gps_collision_probe.cpp src/gps_collision_probe_tests.cpp)
    target_compile_features(ruffneckk_mapsense_gps_collision_probe_tests PRIVATE cxx_std_20)
    target_compile_definitions(ruffneckk_mapsense_gps_collision_probe_tests PRIVATE
        WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE
        RUFFNECKK_MAPSENSE_ENABLE_GPS_COLLISION_PROBE=0)
    target_link_libraries(ruffneckk_mapsense_gps_collision_probe_tests PRIVATE
        D2RLPlugin::D2RLPlugin bcrypt)
    if(MSVC)
        target_compile_options(ruffneckk_mapsense_gps_collision_probe_tests PRIVATE
            /W4 /permissive- /EHsc /utf-8
            $<$<BOOL:${RUFFNECKK_SUITE_WARNINGS_AS_ERRORS}>:/WX>)
    endif()
    add_test(NAME ruffneckk.mapsense.gps-collision-probe
        COMMAND ruffneckk_mapsense_gps_collision_probe_tests)

    add_executable(ruffneckk_mapsense_gps_collision_artifact_tests
        src/gps_collision_artifact.cpp src/gps_collision_artifact_tests.cpp)
    target_compile_features(ruffneckk_mapsense_gps_collision_artifact_tests PRIVATE cxx_std_20)
    target_compile_definitions(ruffneckk_mapsense_gps_collision_artifact_tests PRIVATE
        WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
    target_link_libraries(ruffneckk_mapsense_gps_collision_artifact_tests PRIVATE bcrypt)
    if(MSVC)
        target_compile_options(ruffneckk_mapsense_gps_collision_artifact_tests PRIVATE
            /W4 /permissive- /EHsc /utf-8
            $<$<BOOL:${RUFFNECKK_SUITE_WARNINGS_AS_ERRORS}>:/WX>)
    endif()
    add_test(NAME ruffneckk.mapsense.gps-collision-artifact
        COMMAND ruffneckk_mapsense_gps_collision_artifact_tests)

    add_executable(ruffneckk_mapsense_object_contract_tests
        src/native_object_interact_contract_tests.cpp)
    target_compile_features(ruffneckk_mapsense_object_contract_tests PRIVATE cxx_std_20)
    target_compile_definitions(ruffneckk_mapsense_object_contract_tests PRIVATE
        WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
    if(MSVC)
        target_compile_options(ruffneckk_mapsense_object_contract_tests PRIVATE
            /W4 /permissive- /EHsc /utf-8
            $<$<BOOL:${RUFFNECKK_SUITE_WARNINGS_AS_ERRORS}>:/WX>)
    endif()
    add_test(NAME ruffneckk.mapsense.object-contract COMMAND ruffneckk_mapsense_object_contract_tests)

    add_executable(ruffneckk_mapsense_gpu_tests
        src/d3d12_gpu_diagnostics.cpp src/d3d12_gpu_diagnostics_tests.cpp)
    target_compile_features(ruffneckk_mapsense_gpu_tests PRIVATE cxx_std_20)
    target_compile_definitions(ruffneckk_mapsense_gpu_tests PRIVATE
        WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE
        RUFFNECKK_MAPSENSE_GPU_DIAGNOSTICS=1)
    target_link_libraries(ruffneckk_mapsense_gpu_tests PRIVATE d3d12 dxgi)
    if(MSVC)
        target_compile_options(ruffneckk_mapsense_gpu_tests PRIVATE
            /W4 /permissive- /EHsc /utf-8
            $<$<BOOL:${RUFFNECKK_SUITE_WARNINGS_AS_ERRORS}>:/WX>)
    endif()
    add_test(NAME ruffneckk.mapsense.gpu COMMAND ruffneckk_mapsense_gpu_tests)

    add_executable(ruffneckk_mapsense_gps_route_tests
        src/gps_route_artifact.cpp
        src/gps_route_provider.cpp
        src/gps_route_artifact_tests.cpp)
    target_compile_features(ruffneckk_mapsense_gps_route_tests PRIVATE cxx_std_20)
    target_link_libraries(ruffneckk_mapsense_gps_route_tests PRIVATE
        D2RLPlugin::D2RLPlugin)
    if(MSVC)
        target_compile_options(ruffneckk_mapsense_gps_route_tests PRIVATE
            /W4 /permissive- /EHsc /utf-8
            $<$<BOOL:${RUFFNECKK_SUITE_WARNINGS_AS_ERRORS}>:/WX>)
    endif()
    add_test(NAME ruffneckk.mapsense.gps-route
        COMMAND ruffneckk_mapsense_gps_route_tests)

    # Optional real-Windows bootstrap probe; no D3D12 import may preload the
    # runtime and conceal an application-directory DLL lookup regression.
    add_executable(ruffneckk_mapsense_gpu_bootstrap_tests
        src/d3d12_gpu_diagnostics.cpp src/d3d12_gpu_bootstrap_tests.cpp)
    target_compile_features(ruffneckk_mapsense_gpu_bootstrap_tests PRIVATE cxx_std_20)
    target_compile_definitions(ruffneckk_mapsense_gpu_bootstrap_tests PRIVATE
        WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE
        RUFFNECKK_MAPSENSE_GPU_DIAGNOSTICS=1)
    if(MSVC)
        target_compile_options(ruffneckk_mapsense_gpu_bootstrap_tests PRIVATE
            /W4 /permissive- /EHsc /utf-8
            $<$<BOOL:${RUFFNECKK_SUITE_WARNINGS_AS_ERRORS}>:/WX>)
    endif()

    add_executable(ruffneckk_mapsense_font_tests
        src/localized_fonts.cpp src/localized_fonts_tests.cpp src/ui_localization.cpp)
    target_compile_features(ruffneckk_mapsense_font_tests PRIVATE cxx_std_20)
    target_compile_definitions(ruffneckk_mapsense_font_tests PRIVATE
        WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
    target_link_libraries(ruffneckk_mapsense_font_tests PRIVATE
        RuffnecKk::Common ruffneckk_mapsense_imgui)
    if(MSVC)
        target_compile_options(ruffneckk_mapsense_font_tests PRIVATE
            /W4 /permissive- /EHsc /utf-8
            $<$<BOOL:${RUFFNECKK_SUITE_WARNINGS_AS_ERRORS}>:/WX>)
    endif()
    add_test(NAME ruffneckk.mapsense.fonts COMMAND ruffneckk_mapsense_font_tests)

    add_executable(ruffneckk_mapsense_settings_panel_tests
        src/imgui_settings_panel.cpp src/imgui_settings_panel_tests.cpp src/ui_localization.cpp)
    target_compile_features(ruffneckk_mapsense_settings_panel_tests PRIVATE cxx_std_20)
    target_compile_definitions(ruffneckk_mapsense_settings_panel_tests PRIVATE
        WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
    target_link_libraries(ruffneckk_mapsense_settings_panel_tests PRIVATE
        RuffnecKk::Common ruffneckk_mapsense_imgui tomlplusplus::tomlplusplus)
    if(MSVC)
        target_compile_options(ruffneckk_mapsense_settings_panel_tests PRIVATE
            /W4 /permissive- /EHsc /utf-8
            $<$<BOOL:${RUFFNECKK_SUITE_WARNINGS_AS_ERRORS}>:/WX>)
    endif()
    add_test(NAME ruffneckk.mapsense.settings-panel COMMAND ruffneckk_mapsense_settings_panel_tests)

    add_executable(ruffneckk_mapsense_tests
        src/external_atlas_cache.cpp
        src/external_atlas_geometry.cpp
        src/gps_route_artifact.cpp
        src/automap_sprite_package.cpp
        src/mapsense_data_catalog.cpp
        src/mapsense_tests.cpp
        src/navigation_engine.cpp
        src/navigation_level_catalog.cpp
        src/navigation_policy.cpp src/navigation_routing.cpp
        src/overlay_scene.cpp
        src/ui_localization.cpp
    )
    target_compile_features(ruffneckk_mapsense_tests PRIVATE cxx_std_20)
    target_compile_definitions(ruffneckk_mapsense_tests PRIVATE
        WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE
    )
    target_link_libraries(ruffneckk_mapsense_tests PRIVATE
        RuffnecKk::Common
        RuffnecKk::NativeStatCompat
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
    add_test(NAME ruffneckk.mapsense.txt-only-catalog
        COMMAND ruffneckk_mapsense_tests --txt-only-regression)
endif()

if(BUILD_TESTING)
    add_executable(ruffneckk_mapsense_txt_mode_tests src/txt_mode.cpp src/txt_mode_tests.cpp)
    target_compile_features(ruffneckk_mapsense_txt_mode_tests PRIVATE cxx_std_20)
    target_compile_definitions(ruffneckk_mapsense_txt_mode_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
    target_link_libraries(ruffneckk_mapsense_txt_mode_tests PRIVATE tomlplusplus::tomlplusplus shell32)
    if(MSVC)
        target_compile_options(ruffneckk_mapsense_txt_mode_tests PRIVATE /W4 /permissive- /EHsc /utf-8
            $<$<BOOL:${RUFFNECKK_SUITE_WARNINGS_AS_ERRORS}>:/WX>)
    endif()
    add_test(NAME ruffneckk.mapsense.txt-mode COMMAND ruffneckk_mapsense_txt_mode_tests)
endif()

if(BUILD_TESTING)
    add_executable(ruffneckk_mapsense_routing_tests
        src/navigation_routing_tests.cpp src/navigation_routing.cpp src/navigation_policy.cpp src/navigation_engine.cpp)
    target_link_libraries(ruffneckk_mapsense_routing_tests PRIVATE tomlplusplus::tomlplusplus)
    target_compile_features(ruffneckk_mapsense_routing_tests PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(ruffneckk_mapsense_routing_tests PRIVATE /W4 /WX /permissive- /EHsc /utf-8)
    endif()
    add_test(NAME ruffneckk.mapsense.adaptive-routing COMMAND ruffneckk_mapsense_routing_tests)
endif()