add_library(RuffnecKkSuiteBuildPolicy INTERFACE)
add_library(RuffnecKk::BuildPolicy ALIAS RuffnecKkSuiteBuildPolicy)

target_compile_features(RuffnecKkSuiteBuildPolicy INTERFACE cxx_std_20)
target_compile_definitions(
    RuffnecKkSuiteBuildPolicy
    INTERFACE
        NOMINMAX
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
)

if(MSVC)
    target_compile_options(
        RuffnecKkSuiteBuildPolicy
        INTERFACE
            $<$<COMPILE_LANGUAGE:CXX>:/permissive->
            $<$<COMPILE_LANGUAGE:CXX>:/W4>
            $<$<COMPILE_LANGUAGE:CXX>:/Zc:__cplusplus>
    )

    if(RUFFNECKK_SUITE_WARNINGS_AS_ERRORS)
        target_compile_options(
            RuffnecKkSuiteBuildPolicy INTERFACE
            $<$<COMPILE_LANGUAGE:CXX>:/WX>
        )
    endif()

    # Make release artifacts byte-for-byte reproducible across clean build
    # directories. Without /Brepro, LINK stamps the PE and debug directory
    # with the wall-clock build time, which breaks component hashes even when
    # every source and compiler input is identical.
    target_link_options(RuffnecKkSuiteBuildPolicy INTERFACE /Brepro)
else()
    target_compile_options(
        RuffnecKkSuiteBuildPolicy
        INTERFACE
            $<$<COMPILE_LANGUAGE:CXX>:-Wall>
            $<$<COMPILE_LANGUAGE:CXX>:-Wextra>
            $<$<COMPILE_LANGUAGE:CXX>:-Wpedantic>
    )

    if(RUFFNECKK_SUITE_WARNINGS_AS_ERRORS)
        target_compile_options(
            RuffnecKkSuiteBuildPolicy INTERFACE
            $<$<COMPILE_LANGUAGE:CXX>:-Werror>
        )
    endif()
endif()

add_library(RuffnecKkSuiteCommon INTERFACE)
add_library(RuffnecKk::Common ALIAS RuffnecKkSuiteCommon)
target_include_directories(RuffnecKkSuiteCommon INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(
    RuffnecKkSuiteCommon
    INTERFACE
        RuffnecKk::BuildPolicy
        D2RLPlugin::D2RLPlugin
)

add_library(RuffnecKkSuiteCommonV4 INTERFACE)
add_library(RuffnecKk::CommonV4 ALIAS RuffnecKkSuiteCommonV4)
target_include_directories(RuffnecKkSuiteCommonV4 INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(
    RuffnecKkSuiteCommonV4
    INTERFACE
        RuffnecKk::BuildPolicy
        D2RLPluginV4::D2RLPluginV4
)

add_library(RuffnecKkSuiteNativeStatCompat STATIC src/native_stat_compat.cpp)
add_library(RuffnecKk::NativeStatCompat ALIAS RuffnecKkSuiteNativeStatCompat)
target_include_directories(RuffnecKkSuiteNativeStatCompat PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(RuffnecKkSuiteNativeStatCompat PUBLIC RuffnecKk::BuildPolicy)

# Optional Maps compatibility is statically linked into each consuming DLL.
# Private SDK-v3 compilation does not change a consumer's own SDK/API selection.
add_library(RuffnecKkSuiteMapsStatCompat STATIC src/maps_stat_compat.cpp)
add_library(RuffnecKk::MapsStatCompat ALIAS RuffnecKkSuiteMapsStatCompat)
target_link_libraries(RuffnecKkSuiteMapsStatCompat PUBLIC RuffnecKk::NativeStatCompat PRIVATE RuffnecKk::Common)
set(RUFFNECKK_MAPS_REFERENCE_DIRECTORY "" CACHE PATH "Directory of exact Maps DLL fixtures for optional offline contract tests")

if(BUILD_TESTING)
    add_executable(ruffneckk_tracked_native_transform_tests tests/tracked_native_transform_tests.cpp)
    target_link_libraries(ruffneckk_tracked_native_transform_tests PRIVATE RuffnecKk::BuildPolicy)
    target_include_directories(ruffneckk_tracked_native_transform_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
    add_test(NAME ruffneckk.common.tracked-native-transform COMMAND ruffneckk_tracked_native_transform_tests)

    add_executable(ruffneckk_native_stat_compat_tests tests/native_stat_compat_tests.cpp)
    target_link_libraries(ruffneckk_native_stat_compat_tests PRIVATE RuffnecKk::NativeStatCompat)
    add_test(NAME ruffneckk.common.native-stat-compat COMMAND ruffneckk_native_stat_compat_tests)

    add_executable(ruffneckk_localization_service_tests tests/localization_tests.cpp)
    target_link_libraries(ruffneckk_localization_service_tests PRIVATE RuffnecKk::Common)
    add_test(NAME ruffneckk.common.localization COMMAND ruffneckk_localization_service_tests)
    if(RUFFNECKK_MAPS_REFERENCE_DIRECTORY)
        add_executable(ruffneckk_maps_stat_compat_tests tests/maps_stat_compat_tests.cpp)
        target_include_directories(ruffneckk_maps_stat_compat_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
        target_link_libraries(ruffneckk_maps_stat_compat_tests PRIVATE RuffnecKk::Common RuffnecKk::MapsStatCompat)
        target_compile_options(ruffneckk_maps_stat_compat_tests PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/EHsc>)
        foreach(profile 0.12.1 0.12.4 0.12.5 0.12.5-revision2 0.12.5-r15)
            if(NOT EXISTS "${RUFFNECKK_MAPS_REFERENCE_DIRECTORY}/d2rl-maps-${profile}.dll")
                message(FATAL_ERROR "Missing exact Maps ${profile} test fixture")
            endif()
            foreach(mask getter getter-setter getter-base)
                add_test(NAME ruffneckk.common.maps-${profile}.${mask}
                    COMMAND ruffneckk_maps_stat_compat_tests "${RUFFNECKK_MAPS_REFERENCE_DIRECTORY}/d2rl-maps-${profile}.dll" ${profile} ${mask})
            endforeach()
        endforeach()
    endif()
endif()

if(BUILD_TESTING)
    add_executable(ruffneckk_ui_input_compat_tests tests/ui_input_compat_tests.cpp)
    target_link_libraries(ruffneckk_ui_input_compat_tests PRIVATE RuffnecKk::Common)
    target_compile_options(ruffneckk_ui_input_compat_tests PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/EHsc>)
    add_test(NAME ruffneckk.common.ui-input-compat COMMAND ruffneckk_ui_input_compat_tests)
endif()
