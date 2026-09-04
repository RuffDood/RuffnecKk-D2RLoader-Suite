cmake_minimum_required(VERSION 3.28)

project(
    RuffnecKkD2RLoaderSuite
    VERSION 1.3.2
    DESCRIPTION "Independent RuffnecKk plugins for D2RLoader"
    LANGUAGES CXX
)

include(CTest)

option(
    RUFFNECKK_SUITE_BUILD_PLUGINS
    "Build the RuffnecKk Suite plugin targets"
    ON
)
option(
    RUFFNECKK_SUITE_WARNINGS_AS_ERRORS
    "Treat warnings from RuffnecKk Suite targets as errors"
    OFF
)
set(
    RUFFNECKK_RELEASE_PLAN_PATH "" CACHE FILEPATH
    "External path to the private next-release registry"
)
set(
    RUFFNECKK_RELEASE_SCHEMA_PATH "" CACHE FILEPATH
    "External path to the private next-release schema"
)
set(
    RUFFNECKK_RELEASE_ALLOWLIST_PATH "" CACHE FILEPATH
    "External path to the private release allowlist"
)

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/PluginSDK/CMakeLists.txt")
    message(FATAL_ERROR "The pinned D2RLoader PluginSDK v3 source is missing.")
endif()
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/PluginSDK-v4/CMakeLists.txt")
    message(FATAL_ERROR "The pinned D2RLoader PluginSDK v4 source is missing.")
endif()

add_subdirectory(third_party/PluginSDK)
add_subdirectory(third_party/PluginSDK-v4)
add_subdirectory(common)

# Keep the repository foundation buildable even when a source checkout omits
# plugin targets intentionally.
if(RUFFNECKK_SUITE_BUILD_PLUGINS)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/plugins/CMakeLists.txt")
        add_subdirectory(plugins)
    else()
        message(STATUS "No approved plugin targets are present yet.")
    endif()
endif()

if(BUILD_TESTING)
    find_program(
        RUFFNECKK_POWERSHELL_EXECUTABLE
        NAMES pwsh powershell
        REQUIRED
    )
    add_test(
        NAME ruffneckk-memory-patch-catalog
        COMMAND
            "${RUFFNECKK_POWERSHELL_EXECUTABLE}"
            -NoProfile
            -ExecutionPolicy Bypass
            -File "${CMAKE_CURRENT_SOURCE_DIR}/scripts/Test-MemoryPatches.ps1"
    )
    add_test(
        NAME ruffneckk-suite-source-policy
        COMMAND
            "${RUFFNECKK_POWERSHELL_EXECUTABLE}"
            -NoProfile
            -ExecutionPolicy Bypass
            -File "${CMAKE_CURRENT_SOURCE_DIR}/scripts/Test-Suite.ps1"
            -RequireAll
    )
    add_test(
        NAME ruffneckk-packaged-plugin-build-policy
        COMMAND
            "${RUFFNECKK_POWERSHELL_EXECUTABLE}"
            -NoProfile
            -ExecutionPolicy Bypass
            -File "${CMAKE_CURRENT_SOURCE_DIR}/scripts/Test-PackagedPluginBuildPolicy.ps1"
            -SelfTest
    )
    if(
        RUFFNECKK_RELEASE_PLAN_PATH OR
        RUFFNECKK_RELEASE_SCHEMA_PATH OR
        RUFFNECKK_RELEASE_ALLOWLIST_PATH
    )
        if(
            NOT EXISTS "${RUFFNECKK_RELEASE_PLAN_PATH}" OR
            NOT EXISTS "${RUFFNECKK_RELEASE_SCHEMA_PATH}" OR
            NOT EXISTS "${RUFFNECKK_RELEASE_ALLOWLIST_PATH}"
        )
            message(FATAL_ERROR
                "Release governance requires explicit existing plan, schema, and allowlist paths."
            )
        endif()
        add_test(
            NAME ruffneckk-next-release-registry
            COMMAND
                "${RUFFNECKK_POWERSHELL_EXECUTABLE}"
                -NoProfile
                -ExecutionPolicy Bypass
                -File "${CMAKE_CURRENT_SOURCE_DIR}/scripts/Test-NextRelease.ps1"
                -PlanPath "${RUFFNECKK_RELEASE_PLAN_PATH}"
                -SchemaPath "${RUFFNECKK_RELEASE_SCHEMA_PATH}"
                -AllowlistPath "${RUFFNECKK_RELEASE_ALLOWLIST_PATH}"
        )
        add_test(
            NAME ruffneckk-release-source-policy
            COMMAND
                "${RUFFNECKK_POWERSHELL_EXECUTABLE}"
                -NoProfile
                -ExecutionPolicy Bypass
                -File "${CMAKE_CURRENT_SOURCE_DIR}/scripts/Test-Suite.ps1"
                -RequireAll
                -AllowlistPath "${RUFFNECKK_RELEASE_ALLOWLIST_PATH}"
        )
        add_test(
            NAME ruffneckk-release-native-write-ownership
            COMMAND
                "${RUFFNECKK_POWERSHELL_EXECUTABLE}"
                -NoProfile
                -ExecutionPolicy Bypass
                -File "${CMAKE_CURRENT_SOURCE_DIR}/scripts/Test-NativeWrites.ps1"
                -AllowlistPath "${RUFFNECKK_RELEASE_ALLOWLIST_PATH}"
        )
    endif()
    add_test(
        NAME ruffneckk-native-write-ownership
        COMMAND
            "${RUFFNECKK_POWERSHELL_EXECUTABLE}"
            -NoProfile
            -ExecutionPolicy Bypass
            -File "${CMAKE_CURRENT_SOURCE_DIR}/scripts/Test-NativeWrites.ps1"
    )
    add_test(
        NAME ruffneckk-native-write-ownership-self-tests
        COMMAND
            "${RUFFNECKK_POWERSHELL_EXECUTABLE}"
            -NoProfile
            -ExecutionPolicy Bypass
            -File "${CMAKE_CURRENT_SOURCE_DIR}/scripts/Test-NativeWrites.ps1"
            -SelfTest
    )
endif()
