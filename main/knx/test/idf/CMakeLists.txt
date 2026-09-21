idf_component_register(
    SRCS
        "test_knx_configuration.cc"
        "../../knx_config.cc"
        "../../knx_object.cc"
        "../../knx_validation.cc"
    INCLUDE_DIRS
        "../.."
        "../../.."
    REQUIRES
        unity
        json
        esp_knx_ip
)
target_compile_definitions(${COMPONENT_LIB} PRIVATE XIAOZHI_KNX_CONFIG_PARSER_ONLY)
