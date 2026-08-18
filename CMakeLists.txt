cmake_minimum_required(VERSION 3.28)

project(
    RuffnecKkD2RLoaderSuite
    VERSION 0.1.0
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

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/PluginSDK/CMakeLists.txt")
    message(FATAL_ERROR "The pinned D2RLoader PluginSDK v3 source is missing.")
endif()

add_subdirectory(third_party/PluginSDK)
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
