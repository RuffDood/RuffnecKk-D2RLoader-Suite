add_library(ruffneckk_equipped_item_to_cube SHARED
    src/plugin.cpp
    src/plugin.rc
)

target_link_libraries(ruffneckk_equipped_item_to_cube PRIVATE RuffnecKk::Common)
target_compile_features(ruffneckk_equipped_item_to_cube PRIVATE cxx_std_20)
set_target_properties(ruffneckk_equipped_item_to_cube PROPERTIES
    OUTPUT_NAME "d2rl-ruffneckk-equipped-item-to-cube"
    PREFIX ""
)

d2rlplugin_embed_config(
    TARGET ruffneckk_equipped_item_to_cube
    FILE "${CMAKE_CURRENT_SOURCE_DIR}/config/ruffneckk-equipped-item-to-cube.toml"
)

if(BUILD_TESTING)
    add_executable(ruffneckk_equipped_item_to_cube_tests tests/policy_tests.cpp)
    target_compile_features(ruffneckk_equipped_item_to_cube_tests PRIVATE cxx_std_20)
    target_include_directories(ruffneckk_equipped_item_to_cube_tests PRIVATE src)
    add_test(NAME ruffneckk.equipped-item-to-cube.policy
        COMMAND ruffneckk_equipped_item_to_cube_tests
            "${CMAKE_CURRENT_SOURCE_DIR}/config/ruffneckk-equipped-item-to-cube.toml")
endif()
