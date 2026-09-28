add_library(ruffneckk_auto_pickup SHARED
    src/plugin.cpp
    src/plugin.rc
)

target_link_libraries(ruffneckk_auto_pickup PRIVATE RuffnecKk::Common)
target_compile_features(ruffneckk_auto_pickup PRIVATE cxx_std_20)
set_target_properties(ruffneckk_auto_pickup PROPERTIES
    OUTPUT_NAME "d2rl-ruffneckk-auto-pickup"
    PREFIX ""
)

d2rlplugin_embed_config(
    TARGET ruffneckk_auto_pickup
    FILE "${CMAKE_CURRENT_SOURCE_DIR}/config/ruffneckk-auto-pickup.toml"
)

if(BUILD_TESTING)
    add_executable(ruffneckk_auto_pickup_tests tests/policy_tests.cpp)
    target_link_libraries(ruffneckk_auto_pickup_tests PRIVATE RuffnecKk::Common)
    target_compile_features(ruffneckk_auto_pickup_tests PRIVATE cxx_std_20)
    target_compile_options(ruffneckk_auto_pickup_tests PRIVATE /EHsc)
    target_include_directories(ruffneckk_auto_pickup_tests PRIVATE src)
    add_test(
        NAME ruffneckk.auto-pickup.policy
        COMMAND ruffneckk_auto_pickup_tests
            "${CMAKE_CURRENT_SOURCE_DIR}/config/ruffneckk-auto-pickup.toml"
    )
    add_executable(ruffneckk_auto_pickup_source_policy_tests tests/source_policy_tests.cpp)
    add_test(
        NAME ruffneckk.auto-pickup.source-policy
        COMMAND ruffneckk_auto_pickup_source_policy_tests
            "${CMAKE_CURRENT_SOURCE_DIR}/src/plugin.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/policy.hpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/auto_pickup_route_api.hpp"
    )
    add_executable(ruffneckk_auto_pickup_route_api_tests tests/route_api_tests.cpp)
    target_compile_features(ruffneckk_auto_pickup_route_api_tests PRIVATE cxx_std_20)
    target_include_directories(ruffneckk_auto_pickup_route_api_tests PRIVATE src)
    add_test(
        NAME ruffneckk.auto-pickup.route-api
        COMMAND ruffneckk_auto_pickup_route_api_tests
    )
endif()
