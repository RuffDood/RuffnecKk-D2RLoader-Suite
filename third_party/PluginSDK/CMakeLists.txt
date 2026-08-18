cmake_minimum_required(VERSION 3.28)

project(D2RLPluginSDKVendor LANGUAGES CXX RC)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/D2RLPluginEmbedConfig.cmake")

add_library(D2RLPlugin INTERFACE)
add_library(D2RLPlugin::D2RLPlugin ALIAS D2RLPlugin)

target_include_directories(D2RLPlugin INTERFACE
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_compile_features(D2RLPlugin INTERFACE cxx_std_20)
