add_library(ruffneckk_enhanced_damage_min_max_fix SHARED
    src/plugin.cpp
    src/plugin.rc
)

target_link_libraries(ruffneckk_enhanced_damage_min_max_fix PRIVATE RuffnecKk::Common)
target_compile_features(ruffneckk_enhanced_damage_min_max_fix PRIVATE cxx_std_20)
set_target_properties(ruffneckk_enhanced_damage_min_max_fix PROPERTIES
    OUTPUT_NAME "d2rl-ruffneckk-enhanced-damage-min-max-fix"
    PREFIX ""
)

d2rlplugin_embed_config(
    TARGET ruffneckk_enhanced_damage_min_max_fix
    FILE "${CMAKE_CURRENT_SOURCE_DIR}/config/ruffneckk-enhanced-damage-min-max-fix.toml"
)

if(BUILD_TESTING)
    add_executable(ruffneckk_enhanced_damage_min_max_fix_tests tests/policy_tests.cpp)
    target_compile_features(ruffneckk_enhanced_damage_min_max_fix_tests PRIVATE cxx_std_20)
    target_include_directories(ruffneckk_enhanced_damage_min_max_fix_tests PRIVATE src)
    add_test(NAME ruffneckk.enhanced-damage-min-max-fix.policy
        COMMAND ruffneckk_enhanced_damage_min_max_fix_tests
            "${CMAKE_CURRENT_SOURCE_DIR}/config/ruffneckk-enhanced-damage-min-max-fix.toml")
endif()
