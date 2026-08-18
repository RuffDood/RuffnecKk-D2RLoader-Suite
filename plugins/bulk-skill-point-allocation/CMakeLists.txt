if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.28)
    project(RuffnecKkBulkSkillPointAllocation LANGUAGES CXX RC)
    include(CTest)
    option(
        RUFFNECKK_SUITE_WARNINGS_AS_ERRORS
        "Treat RuffnecKk Suite warnings as errors"
        OFF
    )
    add_subdirectory(../../third_party/PluginSDK "${CMAKE_BINARY_DIR}/_deps/PluginSDK")
    add_subdirectory(../../common "${CMAKE_BINARY_DIR}/_deps/common")
endif()

add_library(ruffneckk_bulk_skill_point_allocation SHARED
    src/plugin.cpp
    src/plugin.rc
)

target_link_libraries(
    ruffneckk_bulk_skill_point_allocation
    PRIVATE
        RuffnecKk::Common
        user32
)
target_compile_features(
    ruffneckk_bulk_skill_point_allocation
    PRIVATE cxx_std_20
)
target_include_directories(
    ruffneckk_bulk_skill_point_allocation
    PRIVATE src
)
set_target_properties(
    ruffneckk_bulk_skill_point_allocation
    PROPERTIES
        OUTPUT_NAME "d2rl-ruffneckk-bulk-skill-point-allocation"
        PREFIX ""
)

if(BUILD_TESTING)
    add_executable(
        ruffneckk_bulk_skill_point_allocation_tests
        tests/policy_tests.cpp
    )
    target_link_libraries(
        ruffneckk_bulk_skill_point_allocation_tests
        PRIVATE RuffnecKk::Common
    )
    target_compile_features(
        ruffneckk_bulk_skill_point_allocation_tests
        PRIVATE cxx_std_20
    )
    target_include_directories(
        ruffneckk_bulk_skill_point_allocation_tests
        PRIVATE src
    )
    add_test(
        NAME ruffneckk.bulk-skill-point-allocation.policy
        COMMAND ruffneckk_bulk_skill_point_allocation_tests
            "${CMAKE_CURRENT_SOURCE_DIR}/config/BulkSkillPointAllocation.json"
            "${CMAKE_CURRENT_SOURCE_DIR}/config/BulkSkillPointAllocation.strings.json"
    )

    add_executable(
        ruffneckk_bulk_skill_point_allocation_metadata_tests
        tests/metadata_tests.cpp
    )
    target_link_libraries(
        ruffneckk_bulk_skill_point_allocation_metadata_tests
        PRIVATE
            RuffnecKk::Common
            ruffneckk_bulk_skill_point_allocation
    )
    target_compile_features(
        ruffneckk_bulk_skill_point_allocation_metadata_tests
        PRIVATE cxx_std_20
    )
    add_test(
        NAME ruffneckk.bulk-skill-point-allocation.metadata
        COMMAND ruffneckk_bulk_skill_point_allocation_metadata_tests
    )
endif()
