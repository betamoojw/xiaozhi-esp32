#pragma once

#include <esp_knx_ip/knx_types.h>

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

enum class KnxDpt {
    kBoolean,
    kUnsigned8,
    kUnsigned16,
    kFloat16,
    kUnsigned32,
    kSigned32,
    kFloat32,
};

using KnxValue = std::variant<bool, uint8_t, uint16_t, float, uint32_t, int32_t>;

struct KnxCommunicationObject {
    std::string id;
    std::string name;
    std::string description;
    std::string group_address;
    knx_address_t parsed_group_address = 0;
    KnxDpt datapoint_type = KnxDpt::kBoolean;
    bool readable = false;
    bool writable = false;
    KnxValue current_value = false;
    bool valid = false;
    uint64_t last_update_ms = 0;
};

bool KnxParseGroupAddress(const std::string& text, knx_address_t& address);
std::string KnxFormatGroupAddress(knx_address_t address);
bool KnxParsePhysicalAddress(const std::string& text, knx_address_t& address);
std::string KnxFormatPhysicalAddress(knx_address_t address);
bool KnxParseDpt(const std::string& text, KnxDpt& datapoint_type);
const char* KnxDptName(KnxDpt datapoint_type);
bool KnxParseValue(KnxDpt datapoint_type, const std::string& text, KnxValue& value);
std::string KnxValueToString(const KnxValue& value);
bool KnxDecodeValue(KnxDpt datapoint_type, const uint8_t* data, size_t length,
                    KnxValue& value);
bool KnxEncodeValue(KnxDpt datapoint_type, const KnxValue& value,
                    std::vector<uint8_t>& data);