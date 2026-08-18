add_library(ruffneckk_potion_auto_pickup SHARED
    src/plugin.cpp
    src/plugin.rc
)

target_link_libraries(ruffneckk_potion_auto_pickup PRIVATE RuffnecKk::Common)
target_compile_features(ruffneckk_potion_auto_pickup PRIVATE cxx_std_20)
set_target_properties(ruffneckk_potion_auto_pickup PROPERTIES
    OUTPUT_NAME "d2rl-ruffneckk-potion-auto-pickup"
    PREFIX ""
)

d2rlplugin_embed_config(
    TARGET ruffneckk_potion_auto_pickup
    FILE "${CMAKE_CURRENT_SOURCE_DIR}/config/ruffneckk-potion-auto-pickup.toml"
)

if(BUILD_TESTING)
    add_executable(ruffneckk_potion_auto_pickup_tests tests/policy_tests.cpp)
    target_link_libraries(ruffneckk_potion_auto_pickup_tests PRIVATE RuffnecKk::Common)
    target_compile_features(ruffneckk_potion_auto_pickup_tests PRIVATE cxx_std_20)
    target_compile_options(ruffneckk_potion_auto_pickup_tests PRIVATE /EHsc)
    target_include_directories(ruffneckk_potion_auto_pickup_tests PRIVATE src)
    add_test(
        NAME ruffneckk.potion-auto-pickup.policy
        COMMAND ruffneckk_potion_auto_pickup_tests
            "${CMAKE_CURRENT_SOURCE_DIR}/config/ruffneckk-potion-auto-pickup.toml"
    )
endif()
