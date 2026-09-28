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

include(FetchContent)
FetchContent_Declare(tomlplusplus GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git GIT_TAG v3.4.0 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(tomlplusplus)

add_library(ruffneckk_bulk_skill_point_allocation SHARED
    src/plugin.cpp
    src/plugin.rc
)

target_link_libraries(
    ruffneckk_bulk_skill_point_allocation
    PRIVATE
        RuffnecKk::Common
        RuffnecKk::NativeStatCompat
        user32
        tomlplusplus::tomlplusplus
)
target_compile_features(
    ruffneckk_bulk_skill_point_allocation
    PRIVATE cxx_std_20
)
target_include_directories(
    ruffneckk_bulk_skill_point_allocation
    PRIVATE src
)
if(MSVC)
    target_compile_options(
        ruffneckk_bulk_skill_point_allocation
        PRIVATE /utf-8
    )
endif()
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
    if(MSVC)
        target_compile_options(
            ruffneckk_bulk_skill_point_allocation_tests
            PRIVATE /utf-8
        )
    endif()
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

if(BUILD_TESTING)
    add_executable(skill_removal_tests tests/removal_tests.cpp)
    target_compile_features(skill_removal_tests PRIVATE cxx_std_20)
    target_include_directories(skill_removal_tests PRIVATE src)
    target_link_libraries(skill_removal_tests PRIVATE RuffnecKk::Common tomlplusplus::tomlplusplus)
    add_test(NAME ruffneckk.bulk-skill-point-allocation.removal COMMAND skill_removal_tests "${CMAKE_CURRENT_SOURCE_DIR}/config/BulkSkillPointAllocation.toml")
endif()
