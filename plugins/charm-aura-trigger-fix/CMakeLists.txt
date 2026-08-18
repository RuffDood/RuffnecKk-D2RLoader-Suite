add_library(ruffneckk_charm_aura_trigger_fix SHARED
    src/plugin.cpp
    src/plugin.rc
)

target_link_libraries(ruffneckk_charm_aura_trigger_fix PRIVATE RuffnecKk::Common)
target_compile_features(ruffneckk_charm_aura_trigger_fix PRIVATE cxx_std_20)
set_target_properties(ruffneckk_charm_aura_trigger_fix PROPERTIES
    OUTPUT_NAME "d2rl-ruffneckk-charm-aura-trigger-fix"
    PREFIX ""
)

d2rlplugin_embed_config(
    TARGET ruffneckk_charm_aura_trigger_fix
    FILE "${CMAKE_CURRENT_SOURCE_DIR}/config/ruffneckk-charm-aura-trigger-fix.toml"
)

if(BUILD_TESTING)
    add_executable(ruffneckk_charm_aura_trigger_fix_tests tests/policy_tests.cpp)
    target_compile_features(ruffneckk_charm_aura_trigger_fix_tests PRIVATE cxx_std_20)
    target_include_directories(ruffneckk_charm_aura_trigger_fix_tests PRIVATE src)
    add_test(NAME ruffneckk.charm-aura-trigger-fix.policy
        COMMAND ruffneckk_charm_aura_trigger_fix_tests
            "${CMAKE_CURRENT_SOURCE_DIR}/config/ruffneckk-charm-aura-trigger-fix.toml")
endif()
