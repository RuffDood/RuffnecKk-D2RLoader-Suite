add_library(ruffneckk_transmute_hotkey SHARED
    src/plugin.cpp
    src/plugin.rc
)

target_link_libraries(ruffneckk_transmute_hotkey PRIVATE RuffnecKk::Common)
target_compile_features(ruffneckk_transmute_hotkey PRIVATE cxx_std_20)
set_target_properties(ruffneckk_transmute_hotkey PROPERTIES
    OUTPUT_NAME "d2rl-ruffneckk-transmute-hotkey"
    PREFIX ""
)

d2rlplugin_embed_config(
    TARGET ruffneckk_transmute_hotkey
    FILE "${CMAKE_CURRENT_SOURCE_DIR}/config/ruffneckk-transmute-hotkey.toml"
)

if(BUILD_TESTING)
    add_executable(ruffneckk_transmute_hotkey_tests tests/policy_tests.cpp)
    target_link_libraries(ruffneckk_transmute_hotkey_tests PRIVATE RuffnecKk::Common)
    target_compile_features(ruffneckk_transmute_hotkey_tests PRIVATE cxx_std_20)
    target_include_directories(ruffneckk_transmute_hotkey_tests PRIVATE src)
    add_test(
        NAME ruffneckk.transmute-hotkey.policy
        COMMAND ruffneckk_transmute_hotkey_tests
            "${CMAKE_CURRENT_SOURCE_DIR}/config/ruffneckk-transmute-hotkey.toml"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/plugin.cpp"
    )
endif()
