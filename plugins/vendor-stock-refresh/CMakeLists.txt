add_library(ruffneckk_vendor_stock_refresh SHARED
    src/plugin.cpp
    src/plugin.rc
)

target_link_libraries(ruffneckk_vendor_stock_refresh PRIVATE RuffnecKk::Common)
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
    add_executable(ruffneckk_vendor_stock_refresh_tests tests/policy_tests.cpp)
    target_compile_features(ruffneckk_vendor_stock_refresh_tests PRIVATE cxx_std_20)
    target_include_directories(ruffneckk_vendor_stock_refresh_tests PRIVATE src)
    add_test(NAME ruffneckk.vendor-stock-refresh.policy
        COMMAND ruffneckk_vendor_stock_refresh_tests
            "${CMAKE_CURRENT_SOURCE_DIR}/config/ruffneckk-vendor-stock-refresh.toml")
endif()
