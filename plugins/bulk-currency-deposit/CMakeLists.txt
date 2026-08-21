if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.28)
    project(RuffnecKkBulkCurrencyDeposit LANGUAGES CXX RC)
    include(CTest)
    option(
        RUFFNECKK_SUITE_WARNINGS_AS_ERRORS
        "Treat warnings from RuffnecKk Suite targets as errors"
        OFF
    )
    add_subdirectory(../../third_party/PluginSDK "${CMAKE_BINARY_DIR}/_deps/PluginSDK")
else()
    enable_language(RC)
endif()

include(FetchContent)
if(NOT TARGET tomlplusplus::tomlplusplus)
    set(TOMLPLUSPLUS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(TOMLPLUSPLUS_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        tomlplusplus
        GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
        GIT_TAG v3.4.0
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(tomlplusplus)
endif()

set(BULK_CURRENCY_DEPOSIT_BUTTON_MOLD
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/button-mold.sprite")
set(BULK_CURRENCY_DEPOSIT_BUTTON_MOLD_LOWEND
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/button-mold.lowend.sprite")
set(BULK_CURRENCY_DEPOSIT_BUTTON
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/deposit-button.sprite")
set(BULK_CURRENCY_DEPOSIT_BUTTON_LOWEND
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/deposit-button.lowend.sprite")

add_library(ruffneckk_bulk_currency_deposit SHARED
    src/plugin.cpp
    src/plugin.rc
)
set_property(
    SOURCE src/plugin.rc
    APPEND PROPERTY OBJECT_DEPENDS
        "${BULK_CURRENCY_DEPOSIT_BUTTON_MOLD};${BULK_CURRENCY_DEPOSIT_BUTTON_MOLD_LOWEND};${BULK_CURRENCY_DEPOSIT_BUTTON};${BULK_CURRENCY_DEPOSIT_BUTTON_LOWEND}"
)
target_compile_features(ruffneckk_bulk_currency_deposit PRIVATE cxx_std_20)
target_link_libraries(ruffneckk_bulk_currency_deposit PRIVATE
    D2RLPlugin::D2RLPlugin
    tomlplusplus::tomlplusplus
)
set_target_properties(ruffneckk_bulk_currency_deposit PROPERTIES
    OUTPUT_NAME "d2rl-ruffneckk-bulk-currency-deposit"
    PREFIX ""
    RUFFNECKK_PUBLIC_ARCHIVE_ELIGIBLE TRUE
)
d2rlplugin_embed_config(
    TARGET ruffneckk_bulk_currency_deposit
    FILE "${CMAKE_CURRENT_SOURCE_DIR}/config/ruffneckk-bulk-currency-deposit.toml"
)
if(MSVC)
    target_compile_options(ruffneckk_bulk_currency_deposit PRIVATE
        /utf-8 /W4 /permissive-
        $<$<BOOL:${RUFFNECKK_SUITE_WARNINGS_AS_ERRORS}>:/WX>
    )
    target_link_options(ruffneckk_bulk_currency_deposit PRIVATE
        /INCREMENTAL:NO /Brepro
    )
endif()

if(BUILD_TESTING)
    add_executable(ruffneckk_bulk_currency_deposit_tests
        src/policy_tests.cpp
    )
    target_compile_features(ruffneckk_bulk_currency_deposit_tests PRIVATE
        cxx_std_20
    )
    target_link_libraries(ruffneckk_bulk_currency_deposit_tests PRIVATE
        tomlplusplus::tomlplusplus
    )
    target_compile_definitions(ruffneckk_bulk_currency_deposit_tests PRIVATE
        BULK_CURRENCY_DEPOSIT_SOURCE_FILE="${CMAKE_CURRENT_SOURCE_DIR}/src/plugin.cpp"
        BULK_CURRENCY_DEPOSIT_BUTTON_FILE="${BULK_CURRENCY_DEPOSIT_BUTTON}"
        BULK_CURRENCY_DEPOSIT_BUTTON_LOWEND_FILE="${BULK_CURRENCY_DEPOSIT_BUTTON_LOWEND}"
    )
    if(MSVC)
        target_compile_options(ruffneckk_bulk_currency_deposit_tests PRIVATE
            /utf-8 /W4 /permissive-
            $<$<BOOL:${RUFFNECKK_SUITE_WARNINGS_AS_ERRORS}>:/WX>
        )
    endif()
    add_test(
        NAME ruffneckk.bulk-currency-deposit.policy
        COMMAND ruffneckk_bulk_currency_deposit_tests
    )
endif()
