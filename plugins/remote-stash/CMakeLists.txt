set(REMOTE_STASH_DEFAULT_SPRITE
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/mod-data/data/hd/global/ui/panel/inventory/remotestashbutton.sprite"
)
set(REMOTE_STASH_DEFAULT_LOWEND_SPRITE
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/mod-data/data/hd/global/ui/panel/inventory/remotestashbutton.lowend.sprite"
)

add_library(ruffneckk_remote_stash SHARED
    src/runtime.cpp
    src/plugin.rc
)
set_property(
    SOURCE src/plugin.rc
    APPEND PROPERTY OBJECT_DEPENDS
        "${REMOTE_STASH_DEFAULT_SPRITE};${REMOTE_STASH_DEFAULT_LOWEND_SPRITE}"
)

target_link_libraries(ruffneckk_remote_stash PRIVATE RuffnecKk::Common)
target_compile_features(ruffneckk_remote_stash PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(ruffneckk_remote_stash PRIVATE /EHsc)
endif()
set_target_properties(ruffneckk_remote_stash PROPERTIES
    OUTPUT_NAME "d2rl-ruffneckk-remote-stash"
    PREFIX ""
)

d2rlplugin_embed_config(
    TARGET ruffneckk_remote_stash
    FILE "${CMAKE_CURRENT_SOURCE_DIR}/config/ruffneckk-remote-stash.toml"
)

if(BUILD_TESTING)
    add_executable(ruffneckk_remote_stash_tests tests/policy_tests.cpp)
    target_link_libraries(ruffneckk_remote_stash_tests PRIVATE RuffnecKk::Common)
    target_compile_features(ruffneckk_remote_stash_tests PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(ruffneckk_remote_stash_tests PRIVATE /EHsc)
    endif()
    target_include_directories(ruffneckk_remote_stash_tests PRIVATE src)
    add_test(
        NAME ruffneckk.remote-stash.policy
        COMMAND ruffneckk_remote_stash_tests
            "${CMAKE_CURRENT_SOURCE_DIR}/config/ruffneckk-remote-stash.toml"
            "${REMOTE_STASH_DEFAULT_SPRITE}"
            "${REMOTE_STASH_DEFAULT_LOWEND_SPRITE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/runtime.cpp"
    )
endif()
