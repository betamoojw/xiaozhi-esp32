cmake_minimum_required(VERSION 3.16)
project(xiaozhi_knx_object_test LANGUAGES C CXX)

# Tests use assert() for both execution and verification, also in Release builds.
if(MSVC)
    add_compile_options(/UNDEBUG)
else()
    add_compile_options(-UNDEBUG)
endif()

if(NOT ESP_KNX_IP_DIR)
    cmake_path(SET ESP_KNX_IP_DIR NORMALIZE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../../managed_components/esp_knx_ip")
endif()

add_executable(knx_object_test
    test_knx_object.cc
    test_knx_validation.cc
    ../knx_object.cc
    ../knx_validation.cc
    ${ESP_KNX_IP_DIR}/src/knx_dpt.cpp
)
target_compile_features(knx_object_test PRIVATE cxx_std_17)
target_include_directories(knx_object_test PRIVATE
    ..
    ${ESP_KNX_IP_DIR}/include
)

add_executable(knx_config_test
    test_knx_config.cc
    ../knx_config.cc
    ../knx_object.cc
    ../knx_validation.cc
    ${ESP_KNX_IP_DIR}/src/knx_dpt.cpp
    ../../../managed_components/espressif__cjson/cJSON/cJSON.c
)
target_compile_features(knx_config_test PRIVATE cxx_std_17)
target_compile_definitions(knx_config_test PRIVATE
    XIAOZHI_KNX_CONFIG_PARSER_ONLY
    KNX_CONFIGURATION_FIXTURE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../../../littlefs/interfaces/knxConfig.json"
)
target_include_directories(knx_config_test PRIVATE
    ..
    ${ESP_KNX_IP_DIR}/include
    ../../../managed_components/espressif__cjson/cJSON
)

enable_testing()
add_test(NAME knx_object_test COMMAND knx_object_test)
add_test(NAME knx_config_test COMMAND knx_config_test)

add_executable(knx_integration_test
    test_knx_integration.cc
    ../knx_manager.cc ../knx_mcp_tools.cc ../knx_config.cc
    ../knx_object.cc ../knx_validation.cc
    ${ESP_KNX_IP_DIR}/src/knx_dpt.cpp
    ../../../managed_components/espressif__cjson/cJSON/cJSON.c
)
target_compile_features(knx_integration_test PRIVATE cxx_std_23)
target_compile_definitions(knx_integration_test PRIVATE XIAOZHI_KNX_CONFIG_PARSER_ONLY)
target_include_directories(knx_integration_test PRIVATE
    fakes .. ../.. ${ESP_KNX_IP_DIR}/include
    ../../../managed_components/espressif__cjson/cJSON
)
add_test(NAME knx_integration_test COMMAND knx_integration_test)
