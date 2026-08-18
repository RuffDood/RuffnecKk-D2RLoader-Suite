add_library(RuffnecKkSuiteCommon INTERFACE)
add_library(RuffnecKk::Common ALIAS RuffnecKkSuiteCommon)

target_link_libraries(
    RuffnecKkSuiteCommon
    INTERFACE
        D2RLPlugin::D2RLPlugin
)
target_compile_features(RuffnecKkSuiteCommon INTERFACE cxx_std_20)
target_compile_definitions(
    RuffnecKkSuiteCommon
    INTERFACE
        NOMINMAX
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
)

if(MSVC)
    target_compile_options(
        RuffnecKkSuiteCommon
        INTERFACE
            /permissive-
            /W4
            /Zc:__cplusplus
    )

    if(RUFFNECKK_SUITE_WARNINGS_AS_ERRORS)
        target_compile_options(RuffnecKkSuiteCommon INTERFACE /WX)
    endif()

    # Make release artifacts byte-for-byte reproducible across clean build
    # directories. Without /Brepro, LINK stamps the PE and debug directory
    # with the wall-clock build time, which breaks component hashes even when
    # every source and compiler input is identical.
    target_link_options(RuffnecKkSuiteCommon INTERFACE /Brepro)
else()
    target_compile_options(
        RuffnecKkSuiteCommon
        INTERFACE
            -Wall
            -Wextra
            -Wpedantic
    )

    if(RUFFNECKK_SUITE_WARNINGS_AS_ERRORS)
        target_compile_options(RuffnecKkSuiteCommon INTERFACE -Werror)
    endif()
endif()
