add_library(ruffneckk_vendor_stock_refresh SHARED
    src/plugin.cpp
    src/plugin.rc
)

target_link_libraries(ruffneckk_vendor_stock_refresh PRIVATE RuffnecKk::CommonV4 bcrypt)
target_compile_features(ruffneckk_vendor_stock_refresh PRIVATE cxx_std_20)
set_target_properties(ruffneckk_vendor_stock_refresh PROPERTIES
    OUTPUT_NAME "d2rl-ruffneckk-vendor-stock-refresh"
    PREFIX ""
)

d2rlplugin_embed_config(
    TARGET ruffneckk_vendor_stock_refresh
    FILE "${CMAKE_CURRENT_SOURCE_DIR}/config/ruffneckk-vendor-stock-refresh.toml"
)

if(BUILD_TESTING)
    set(VENDOR_COMPANION_TEST_MPQ "" CACHE FILEPATH "Matching companion MPQ for compiled-DLL acceptance tests")
    if(EXISTS "${VENDOR_COMPANION_TEST_MPQ}")
        add_executable(ruffneckk_vendor_companion_tests tests/companion_tests.cpp)
        target_include_directories(ruffneckk_vendor_companion_tests PRIVATE src)
        target_compile_features(ruffneckk_vendor_companion_tests PRIVATE cxx_std_20)
        target_link_libraries(ruffneckk_vendor_companion_tests PRIVATE RuffnecKk::CommonV4 ruffneckk_vendor_stock_refresh)
        if(MSVC)
            target_compile_options(ruffneckk_vendor_companion_tests PRIVATE /W4 /WX)
        endif()
        add_test(NAME ruffneckk.vendor-stock-refresh.companion
            COMMAND ruffneckk_vendor_companion_tests "${VENDOR_COMPANION_TEST_MPQ}")
    endif()
    add_executable(ruffneckk_vendor_stock_refresh_panel_tests tests/panel_layout_tests.cpp)
    target_compile_features(ruffneckk_vendor_stock_refresh_panel_tests PRIVATE cxx_std_20)
    target_include_directories(ruffneckk_vendor_stock_refresh_panel_tests PRIVATE src)
    if(MSVC)
        target_compile_options(ruffneckk_vendor_stock_refresh_panel_tests PRIVATE /W4 /WX)
    endif()
    add_test(NAME ruffneckk.vendor-stock-refresh.panel-layout
        COMMAND ruffneckk_vendor_stock_refresh_panel_tests)
    find_program(VENDOR_PANEL_NODE NAMES node REQUIRED)
    add_test(NAME ruffneckk.vendor-stock-refresh.panel-assets
        COMMAND "${VENDOR_PANEL_NODE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/test-panel-assets.cjs")
    add_executable(ruffneckk_vendor_stock_refresh_layout_tests tests/compact_layout_tests.cpp)
    target_compile_features(ruffneckk_vendor_stock_refresh_layout_tests PRIVATE cxx_std_20)
    target_include_directories(ruffneckk_vendor_stock_refresh_layout_tests PRIVATE src)
    if(MSVC)
        target_compile_options(ruffneckk_vendor_stock_refresh_layout_tests PRIVATE /W4 /WX)
    endif()
    add_test(NAME ruffneckk.vendor-stock-refresh.compact-layout
        COMMAND ruffneckk_vendor_stock_refresh_layout_tests)
    set(RUFFNECKK_VENDOR_STOCK_REFRESH_D2RCORE_CANDIDATE "" CACHE FILEPATH
        "Optional D2RCore.dll used by Vendor Stock Refresh's exact PE contract test")
    add_executable(ruffneckk_vendor_stock_refresh_tests tests/policy_tests.cpp)
    target_compile_features(ruffneckk_vendor_stock_refresh_tests PRIVATE cxx_std_20)
    target_include_directories(ruffneckk_vendor_stock_refresh_tests PRIVATE src)
    if(MSVC)
        target_compile_options(ruffneckk_vendor_stock_refresh_tests PRIVATE /W4 /WX)
    endif()
    set(ruffneckk_vendor_stock_refresh_test_arguments
        "${CMAKE_CURRENT_SOURCE_DIR}/config/ruffneckk-vendor-stock-refresh.toml")
    if(EXISTS "${RUFFNECKK_VENDOR_STOCK_REFRESH_D2RCORE_CANDIDATE}")
        list(APPEND ruffneckk_vendor_stock_refresh_test_arguments
            "${RUFFNECKK_VENDOR_STOCK_REFRESH_D2RCORE_CANDIDATE}")
    endif()
    add_test(NAME ruffneckk.vendor-stock-refresh.policy
        COMMAND ruffneckk_vendor_stock_refresh_tests
            ${ruffneckk_vendor_stock_refresh_test_arguments})

    add_executable(
        ruffneckk_vendor_stock_refresh_metadata_tests
        tests/metadata_tests.cpp
    )
    target_link_libraries(
        ruffneckk_vendor_stock_refresh_metadata_tests
        PRIVATE
            RuffnecKk::CommonV4
            ruffneckk_vendor_stock_refresh
    )
    target_compile_features(
        ruffneckk_vendor_stock_refresh_metadata_tests
        PRIVATE cxx_std_20
    )
    if(MSVC)
        target_compile_options(
            ruffneckk_vendor_stock_refresh_metadata_tests
            PRIVATE /W4 /WX
        )
    endif()
    add_test(
        NAME ruffneckk.vendor-stock-refresh.metadata
        COMMAND ruffneckk_vendor_stock_refresh_metadata_tests
    )
endif()

option(VENDOR_LAYOUT_DIAGNOSTIC "Build bounded automatic vendor layout diagnostics" OFF)
if(VENDOR_LAYOUT_DIAGNOSTIC)
    target_compile_definitions(ruffneckk_vendor_stock_refresh PRIVATE VENDOR_LAYOUT_DIAGNOSTIC=1)
endif()

if(BUILD_TESTING AND VENDOR_LAYOUT_DIAGNOSTIC)
    add_executable(ruffneckk_vendor_layout_diagnostic_tests tests/layout_diagnostic_tests.cpp)
    target_compile_features(ruffneckk_vendor_layout_diagnostic_tests PRIVATE cxx_std_20)
    target_compile_definitions(ruffneckk_vendor_layout_diagnostic_tests PRIVATE VENDOR_LAYOUT_DIAGNOSTIC=1)
    target_link_libraries(ruffneckk_vendor_layout_diagnostic_tests PRIVATE RuffnecKk::CommonV4 bcrypt)
    if(MSVC)
        target_compile_options(ruffneckk_vendor_layout_diagnostic_tests PRIVATE /W4 /WX)
    endif()
    add_test(NAME ruffneckk.vendor-stock-refresh.layout-diagnostic COMMAND ruffneckk_vendor_layout_diagnostic_tests)
endif()
