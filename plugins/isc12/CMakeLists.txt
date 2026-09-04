if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.28)
    project(RuffnecKkISC12 VERSION 1.0.2 LANGUAGES CXX RC ASM_MASM)
    include(CTest)
    option(RUFFNECKK_SUITE_WARNINGS_AS_ERRORS "Treat warnings as errors" OFF)
    add_subdirectory(../../third_party/PluginSDK "${CMAKE_BINARY_DIR}/_deps/PluginSDK")
else()
    enable_language(RC)
    enable_language(ASM_MASM)
endif()

add_library(ruffneckk_isc12 SHARED
    src/plugin.cpp
    src/isc12_codec_patch.cpp
    src/isc12_envelope.cpp
    src/isc12_item_packet_budget.cpp
    src/isc12_native_persistence_adapter.cpp
    src/isc12_native_schema_adapter.cpp
    src/isc12_persistence_policy.cpp
    src/isc12_player_stat_preflight.cpp
    src/isc12_publication_adapters.cpp
    src/isc12_publication_coordinator.cpp
    src/isc12_schema.cpp
    src/isc12_loader.cpp
    src/isc12_loader_tail.asm
    src/isc12_persistence_relay.asm
    src/plugin.rc
)
target_compile_features(ruffneckk_isc12 PRIVATE cxx_std_20)
target_compile_definitions(ruffneckk_isc12 PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE
)
target_link_libraries(ruffneckk_isc12 PRIVATE
    D2RLPlugin::D2RLPlugin bcrypt version
)
set_target_properties(ruffneckk_isc12 PROPERTIES
    OUTPUT_NAME "d2rl-ruffneckk-isc12"
    PREFIX ""
    RUFFNECKK_PUBLIC_ARCHIVE_ELIGIBLE TRUE
)
if(MSVC)
    target_compile_options(ruffneckk_isc12 PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:/W4>
        $<$<COMPILE_LANGUAGE:CXX>:/permissive->
        $<$<COMPILE_LANGUAGE:CXX>:/EHsc>
        $<$<COMPILE_LANGUAGE:CXX>:/utf-8>
        $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<BOOL:${RUFFNECKK_SUITE_WARNINGS_AS_ERRORS}>>:/WX>
    )
    target_link_options(ruffneckk_isc12 PRIVATE /INCREMENTAL:NO /Brepro)
endif()

if(BUILD_TESTING)
    add_executable(ruffneckk_isc12_tests
        src/policy_tests.cpp
        src/isc12_atomic_file.cpp
        src/isc12_codec_patch.cpp
        src/isc12_envelope.cpp
        src/isc12_item_packet_budget.cpp
        src/isc12_persistence_policy.cpp
        src/isc12_player_stat_preflight.cpp
        src/isc12_schema.cpp
    )
    target_compile_features(ruffneckk_isc12_tests PRIVATE cxx_std_20)
    target_compile_definitions(ruffneckk_isc12_tests PRIVATE
        ISC12_CODEC_PATCH_TESTING=1
        ISC12_PLUGIN_SOURCE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/src/plugin.cpp"
        ISC12_RESOURCE_SOURCE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/src/plugin.rc"
        ISC12_LOADER_SOURCE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/src/isc12_loader.cpp"
        ISC12_LOADER_ASM_PATH="${CMAKE_CURRENT_SOURCE_DIR}/src/isc12_loader_tail.asm"
        ISC12_PERSISTENCE_ASM_PATH="${CMAKE_CURRENT_SOURCE_DIR}/src/isc12_persistence_relay.asm"
        ISC12_CMAKE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt"
        ISC12_SOURCE_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    target_link_libraries(ruffneckk_isc12_tests PRIVATE bcrypt)

    add_executable(ruffneckk_isc12_native_schema_adapter_tests
        src/native_schema_adapter_tests.cpp
        src/isc12_envelope.cpp
        src/isc12_native_schema_adapter.cpp
        src/isc12_schema.cpp
    )
    target_compile_features(ruffneckk_isc12_native_schema_adapter_tests PRIVATE cxx_std_20)
    target_link_libraries(ruffneckk_isc12_native_schema_adapter_tests PRIVATE bcrypt)

    add_executable(ruffneckk_isc12_native_persistence_adapter_tests
        src/native_persistence_adapter_tests.cpp
        src/isc12_envelope.cpp
        src/isc12_native_persistence_adapter.cpp
        src/isc12_persistence_policy.cpp
    )
    target_compile_features(ruffneckk_isc12_native_persistence_adapter_tests PRIVATE cxx_std_20)
    target_link_libraries(ruffneckk_isc12_native_persistence_adapter_tests PRIVATE bcrypt)

    add_executable(ruffneckk_isc12_publication_coordinator_tests
        src/publication_coordinator_tests.cpp
        src/isc12_publication_coordinator.cpp
    )
    target_compile_features(ruffneckk_isc12_publication_coordinator_tests PRIVATE cxx_std_20)
    target_compile_definitions(ruffneckk_isc12_publication_coordinator_tests PRIVATE ISC12_CODEC_PATCH_TESTING=1)

    add_executable(ruffneckk_isc12_publication_adapter_tests
        src/publication_adapter_tests.cpp
        src/isc12_codec_patch.cpp
        src/isc12_publication_adapters.cpp
        src/isc12_publication_coordinator.cpp
    )
    target_compile_features(ruffneckk_isc12_publication_adapter_tests PRIVATE cxx_std_20)
    target_compile_definitions(ruffneckk_isc12_publication_adapter_tests PRIVATE ISC12_CODEC_PATCH_TESTING=1)

    foreach(test_target IN ITEMS
        ruffneckk_isc12_tests
        ruffneckk_isc12_native_schema_adapter_tests
        ruffneckk_isc12_native_persistence_adapter_tests
        ruffneckk_isc12_publication_coordinator_tests
        ruffneckk_isc12_publication_adapter_tests
    )
        if(MSVC)
            target_compile_options(${test_target} PRIVATE
                /W4 /permissive- /EHsc /utf-8
                $<$<BOOL:${RUFFNECKK_SUITE_WARNINGS_AS_ERRORS}>:/WX>
            )
        endif()
    endforeach()
    if(MSVC)
        target_link_options(ruffneckk_isc12_tests PRIVATE /STACK:4194304)
    endif()

    add_test(NAME ruffneckk.isc12.policy COMMAND ruffneckk_isc12_tests)
    add_test(NAME ruffneckk.isc12.native-schema-adapter COMMAND ruffneckk_isc12_native_schema_adapter_tests)
    add_test(NAME ruffneckk.isc12.native-persistence-adapter COMMAND ruffneckk_isc12_native_persistence_adapter_tests)
    add_test(NAME ruffneckk.isc12.publication-coordinator COMMAND ruffneckk_isc12_publication_coordinator_tests)
    add_test(NAME ruffneckk.isc12.publication-adapters COMMAND ruffneckk_isc12_publication_adapter_tests)
endif()
