if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.28)
    project(RuffnecKkLarzukSockets LANGUAGES CXX RC)
    include(CTest)
    add_subdirectory(../../third_party/PluginSDK "${CMAKE_BINARY_DIR}/_deps/PluginSDK")
    add_subdirectory(../../common "${CMAKE_BINARY_DIR}/_deps/common")
endif()

add_library(ruffneckk_larzuk_sockets SHARED
    src/plugin.cpp
    src/plugin.rc
)

target_link_libraries(ruffneckk_larzuk_sockets PRIVATE RuffnecKk::Common)
target_compile_features(ruffneckk_larzuk_sockets PRIVATE cxx_std_20)
target_include_directories(ruffneckk_larzuk_sockets PRIVATE src)
target_include_directories(
    ruffneckk_larzuk_sockets
    SYSTEM PRIVATE
        third_party/nlohmann
)
set_target_properties(ruffneckk_larzuk_sockets PROPERTIES
    OUTPUT_NAME "d2rl-ruffneckk-larzuk-sockets"
    PREFIX ""
)

if(BUILD_TESTING)
    add_executable(ruffneckk_larzuk_sockets_tests tests/policy_tests.cpp)
    target_link_libraries(ruffneckk_larzuk_sockets_tests PRIVATE RuffnecKk::Common)
    target_compile_features(ruffneckk_larzuk_sockets_tests PRIVATE cxx_std_20)
    target_include_directories(ruffneckk_larzuk_sockets_tests PRIVATE src)
    target_include_directories(
        ruffneckk_larzuk_sockets_tests
        SYSTEM PRIVATE
            third_party/nlohmann
    )
    add_test(
        NAME ruffneckk.larzuk-sockets.policy
        COMMAND ruffneckk_larzuk_sockets_tests
            "${CMAKE_CURRENT_SOURCE_DIR}/config/ForceLarzukSockets.json"
    )

    add_executable(
        ruffneckk_larzuk_sockets_metadata_tests
        tests/metadata_tests.cpp
    )
    target_link_libraries(
        ruffneckk_larzuk_sockets_metadata_tests
        PRIVATE
            RuffnecKk::Common
            ruffneckk_larzuk_sockets
    )
    target_compile_features(
        ruffneckk_larzuk_sockets_metadata_tests
        PRIVATE cxx_std_20
    )
    add_test(
        NAME ruffneckk.larzuk-sockets.metadata
        COMMAND ruffneckk_larzuk_sockets_metadata_tests
    )
endif()
