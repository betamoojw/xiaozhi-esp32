cmake_minimum_required(VERSION 3.16)
project(xiaozhi_knx_object_test LANGUAGES CXX)

if(NOT ESP_KNX_IP_ROOT)
    message(FATAL_ERROR "Set ESP_KNX_IP_ROOT to the esp32_knx_ip repository root")
endif()

add_executable(knx_object_test
    test_knx_object.cc
    ../knx_object.cc
    ${ESP_KNX_IP_ROOT}/components/esp_knx_ip/src/knx_dpt.cpp
)
target_compile_features(knx_object_test PRIVATE cxx_std_17)
target_include_directories(knx_object_test PRIVATE
    ..
    ${ESP_KNX_IP_ROOT}/components/esp_knx_ip/include
)

enable_testing()
add_test(NAME knx_object_test COMMAND knx_object_test)