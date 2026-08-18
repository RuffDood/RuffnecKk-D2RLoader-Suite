add_library(ruffneckk_cube_quick_move SHARED
    src/plugin.cpp
    src/plugin.rc
)

target_link_libraries(ruffneckk_cube_quick_move PRIVATE RuffnecKk::Common)
target_compile_features(ruffneckk_cube_quick_move PRIVATE cxx_std_20)
set_target_properties(ruffneckk_cube_quick_move PROPERTIES
    OUTPUT_NAME "d2rl-ruffneckk-cube-quick-move"
    PREFIX ""
)

d2rlplugin_embed_config(
    TARGET ruffneckk_cube_quick_move
    FILE "${CMAKE_CURRENT_SOURCE_DIR}/config/ruffneckk-cube-quick-move.toml"
)

if(BUILD_TESTING)
    add_executable(ruffneckk_cube_quick_move_tests tests/policy_tests.cpp)
    target_compile_features(ruffneckk_cube_quick_move_tests PRIVATE cxx_std_20)
    target_include_directories(ruffneckk_cube_quick_move_tests PRIVATE src)
    add_test(NAME ruffneckk.cube-quick-move.policy
        COMMAND ruffneckk_cube_quick_move_tests
            "${CMAKE_CURRENT_SOURCE_DIR}/config/ruffneckk-cube-quick-move.toml")
endif()
