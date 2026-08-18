add_library(ruffneckk_prevent_merc_death_in_town SHARED
    src/plugin.cpp
    src/plugin.rc
)

target_link_libraries(ruffneckk_prevent_merc_death_in_town PRIVATE RuffnecKk::Common)
target_compile_features(ruffneckk_prevent_merc_death_in_town PRIVATE cxx_std_20)
set_target_properties(ruffneckk_prevent_merc_death_in_town PROPERTIES
    OUTPUT_NAME "d2rl-ruffneckk-prevent-merc-death-in-town"
    PREFIX ""
)

d2rlplugin_embed_config(
    TARGET ruffneckk_prevent_merc_death_in_town
    FILE "${CMAKE_CURRENT_SOURCE_DIR}/config/ruffneckk-prevent-merc-death-in-town.toml"
)

if(BUILD_TESTING)
    add_executable(ruffneckk_prevent_merc_death_in_town_tests tests/policy_tests.cpp)
    target_compile_features(ruffneckk_prevent_merc_death_in_town_tests PRIVATE cxx_std_20)
    target_include_directories(ruffneckk_prevent_merc_death_in_town_tests PRIVATE src)
    add_test(NAME ruffneckk.prevent-merc-death-in-town.policy
        COMMAND ruffneckk_prevent_merc_death_in_town_tests
            "${CMAKE_CURRENT_SOURCE_DIR}/config/ruffneckk-prevent-merc-death-in-town.toml")
endif()
