#include "knx_config.h"

#include <unity.h>

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

bool Parse(const std::string& json, size_t maximum_objects,
           std::vector<KnxCommunicationObject>& objects, std::string& error) {
    std::string canonical_json;
    return KnxParseConfiguration(json, maximum_objects, maximum_objects,
                                 objects, canonical_json, error);
}

}  // namespace

TEST_CASE("KNX configuration accepts a valid registry", "[knx][configuration]") {
    std::vector<KnxCommunicationObject> objects;
    std::string canonical_json;
    std::string error;
    TEST_ASSERT_TRUE(KnxParseConfiguration(kValidConfiguration, 8, 8, objects,
                                           canonical_json, error));
    TEST_ASSERT_EQUAL(3, objects.size());
    TEST_ASSERT_EQUAL_STRING("test_switch_command", objects[0].id.c_str());
    TEST_ASSERT_FALSE(objects[0].readable);
    TEST_ASSERT_TRUE(objects[0].writable);
    TEST_ASSERT_EQUAL_STRING("test_temperature", objects[2].id.c_str());
    TEST_ASSERT_EQUAL(static_cast<int>(KnxDpt::kFloat16),
              static_cast<int>(objects[2].datapoint_type));
    TEST_ASSERT_FALSE(canonical_json.empty());
}

TEST_CASE("KNX configuration rejects malformed JSON", "[knx][configuration]") {
    std::vector<KnxCommunicationObject> objects;
    std::string error;
    TEST_ASSERT_FALSE(Parse("[{", 8, objects, error));
}

TEST_CASE("KNX configuration rejects unsupported DPT", "[knx][configuration]") {
    std::vector<KnxCommunicationObject> objects;
    std::string error;
    std::string json = kValidConfiguration;
    json.replace(json.find("DPT-1.001"), 9, "DPT-17.001");
    TEST_ASSERT_FALSE(Parse(json, 8, objects, error));
}

TEST_CASE("KNX configuration rejects invalid and duplicate addresses",
          "[knx][configuration]") {
    std::vector<KnxCommunicationObject> objects;
    std::string error;
    std::string invalid = kValidConfiguration;
    invalid.replace(invalid.find("1/0/1"), 5, "32/0/1");
    TEST_ASSERT_FALSE(Parse(invalid, 8, objects, error));

    std::string duplicate = kValidConfiguration;
    duplicate.replace(duplicate.find("2/0/1"), 5, "1/0/1");
    TEST_ASSERT_FALSE(Parse(duplicate, 8, objects, error));
}

TEST_CASE("KNX configuration rejects duplicate IDs and invalid permissions",
          "[knx][configuration]") {
    std::vector<KnxCommunicationObject> objects;
    std::string error;
    std::string duplicate = kValidConfiguration;
    duplicate.replace(duplicate.find("test_switch_status"), 18,
              "test_switch_command");
    TEST_ASSERT_FALSE(Parse(duplicate, 8, objects, error));

    constexpr char kNoPermissions[] = R"json([{
      "id":"disabled",
      "name":"Disabled",
      "group_address":"1/0/1",
      "datapoint_type":"DPT-1",
      "readable":false,
      "writable":false
    }])json";
    TEST_ASSERT_FALSE(Parse(kNoPermissions, 8, objects, error));
}

TEST_CASE("KNX configuration enforces object limits", "[knx][configuration]") {
    std::vector<KnxCommunicationObject> objects;
    std::string error;
    TEST_ASSERT_FALSE(Parse(kValidConfiguration, 1, objects, error));
}

TEST_CASE("KNX configuration uses the asset interface registry path", "[knx][configuration]") {
  TEST_ASSERT_EQUAL_STRING("assets/interfaces/knxConfig.json",
               kKnxConfigurationPath);
}
