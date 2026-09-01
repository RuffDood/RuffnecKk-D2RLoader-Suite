cmake_minimum_required(VERSION 3.28)

project(D2RLPluginSDKV4Vendor LANGUAGES CXX RC)

add_library(D2RLPluginV4 INTERFACE)
add_library(D2RLPluginV4::D2RLPluginV4 ALIAS D2RLPluginV4)

target_include_directories(D2RLPluginV4 INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_compile_features(D2RLPluginV4 INTERFACE cxx_std_20)
