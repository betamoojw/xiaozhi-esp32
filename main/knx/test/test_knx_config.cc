#include "knx_config.h"

#include <cassert>
#include <iostream>
#include <string>
#include <vector>

namespace {

constexpr char kValidConfiguration[] = R"json([
    {
        "id": "test_switch_command",
        "name": "Test Switch Command",
        "description": "Generic writable boolean used for KNX integration testing",
        "group_address": "1/0/1",
        "datapoint_type": "DPT-1.001",
        "readable": false,
        "writable": true
    },
    {
        "id": "test_switch_status",
        "name": "Test Switch Status",
        "description": "Generic boolean feedback used for KNX integration testing",
        "group_address": "1/0/2",
        "datapoint_type": "DPT-1.001",
        "readable": true,
        "writable": false
    },
    {
        "id": "test_temperature",
        "name": "Test Temperature",
        "description": "Generic two-byte floating-point sensor used for KNX integration testing",
        "group_address": "2/0/1",
        "datapoint_type": "DPT-9.001",
        "readable": true,
        "writable": false
    }
])json";

constexpr char kValidConfigurationNewFormat[] = R"json({
    "media_type": "knx_ip",
    "media_parameters": {
        "multicast_address": "224.0.23.12",
        "udp_port": 3671,
        "transport_mode": "routing",
        "interface_identifier": "KNX-IP-Interface",
        "nat": false
    },
    "physical_address": "15.15.199",
    "communication_objects": [
        {
            "id": "test_switch_command",
            "name": "Test Switch Command",
            "description": "Generic writable boolean used for KNX integration testing",
            "group_address": "1/0/1",
            "datapoint_type": "DPT-1.001",
            "readable": false,
            "writable": true,
            "unit": null
        },
        {
            "id": "test_switch_status",
            "name": "Test Switch Status",
            "description": "Generic boolean feedback used for KNX integration testing",
            "group_address": "1/0/2",
            "datapoint_type": "DPT-1.001",
            "readable": true,
            "writable": false,
            "unit": null
        },
        {
            "id": "test_temperature",
            "name": "Test Temperature",
            "description": "Generic two-byte floating-point sensor used for KNX integration testing",
            "group_address": "2/0/1",
            "datapoint_type": "DPT-9.001",
            "readable": true,
            "writable": false,
            "unit": "°C"
        }
    ]
})json";

bool Parse(const std::string& json, size_t maximum_objects = 8, size_t maximum_addresses = 8) {
    std::vector<KnxCommunicationObject> objects;
    std::string canonical_json;
    std::string error;
    return KnxParseConfiguration(json, maximum_objects, maximum_addresses, objects, canonical_json,
                                 error);
}

void TestValidConfiguration() {
    std::vector<KnxCommunicationObject> objects;
    std::string canonical_json;
    std::string error;
    assert(KnxParseConfiguration(kValidConfiguration, 8, 8, objects, canonical_json, error));
    assert(objects.size() == 3);
    assert(objects[0].id == "test_switch_command");
    assert(!objects[0].readable && objects[0].writable);
    assert(objects[1].readable && !objects[1].writable);
    assert(objects[2].id == "test_temperature");
    assert((objects[2].datapoint_type == KnxDpt{9, 1, true}));
    assert(KnxDptName(objects[2].datapoint_type) == "DPT-9.001");
    assert(!canonical_json.empty());
}

void TestMalformedAndUnsupportedValues() {
    assert(!Parse("[{"));
    std::string unsupported = kValidConfiguration;
    unsupported.replace(unsupported.find("DPT-1.001"), 9, "DPT-32.001");
    assert(!Parse(unsupported));

    std::string newly_supported = kValidConfiguration;
    newly_supported.replace(newly_supported.find("DPT-1.001"), 9, "DPT-17.001");
    assert(Parse(newly_supported));
}

void TestAddressesAndIds() {
    std::string invalid_address = kValidConfiguration;
    invalid_address.replace(invalid_address.find("1/0/1"), 5, "32/0/1");
    assert(!Parse(invalid_address));

    std::string duplicate_address = kValidConfiguration;
    duplicate_address.replace(duplicate_address.find("2/0/1"), 5, "1/0/1");
    assert(!Parse(duplicate_address));

    std::string duplicate_id = kValidConfiguration;
    duplicate_id.replace(duplicate_id.find("test_switch_status"), 18, "test_switch_command");
    assert(!Parse(duplicate_id));
}

void TestPermissionsAndLimits() {
    constexpr char kNoPermissions[] = R"json([{
      "id":"disabled","name":"Disabled","group_address":"1/0/1",
      "datapoint_type":"DPT-1","readable":false,"writable":false
    }])json";
    assert(!Parse(kNoPermissions));
    assert(!Parse(kValidConfiguration, 1));
    assert(!Parse(kValidConfiguration, 8, 1));
}

void TestEmptyRegistry() {
    std::vector<KnxCommunicationObject> objects;
    std::string canonical_json;
    std::string error;
    assert(KnxParseConfiguration("[]", 8, 8, objects, canonical_json, error));
    assert(objects.empty());
    assert(canonical_json == "[]");
}

void TestNewFormatConfiguration() {
    std::vector<KnxCommunicationObject> objects;
    std::string canonical_json;
    std::string error;
    assert(KnxParseConfiguration(kValidConfigurationNewFormat, 8, 8, objects, canonical_json,
                                 error));
    assert(objects.size() == 3);
    assert(objects[0].id == "test_switch_command");
    assert(!objects[0].readable && objects[0].writable);
    assert(objects[1].readable && !objects[1].writable);
    assert(objects[2].id == "test_temperature");
    assert((objects[2].datapoint_type == KnxDpt{9, 1, true}));
    assert(objects[2].unit == "°C");
    assert(!canonical_json.empty());
}

void TestNewFormatWithUnits() {
    std::vector<KnxCommunicationObject> objects;
    std::string canonical_json;
    std::string error;
    assert(KnxParseConfiguration(kValidConfigurationNewFormat, 8, 8, objects, canonical_json,
                                 error));
    // Check that units are properly parsed
    assert(objects[0].unit.empty());  // null in JSON becomes empty string
    assert(objects[2].unit == "°C");
}

}  // namespace

int main() {
    TestValidConfiguration();
    TestMalformedAndUnsupportedValues();
    TestAddressesAndIds();
    TestPermissionsAndLimits();
    TestEmptyRegistry();
    TestNewFormatConfiguration();
    TestNewFormatWithUnits();
    std::cout << "All KNX configuration tests passed\n";
    return 0;
}