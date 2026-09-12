#pragma once

#include <esp_knx_ip/knx_dpt.h>
#include <esp_knx_ip/knx_types.h>

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

struct KnxDpt {
    uint16_t main = 1;
    uint16_t subtype = 0;
    bool has_subtype = false;

    bool operator==(const KnxDpt& other) const {
        return main == other.main && subtype == other.subtype && has_subtype == other.has_subtype;
    }
};

using KnxValue = std::variant<bool, uint8_t, int8_t, uint16_t, int16_t, uint32_t, int32_t, int64_t,
                              float, std::string, knx_dpt2_control_t, knx_dpt3_control_t,
                              knx_dpt10_time_t, knx_dpt11_date_t, knx_dpt18_scene_control_t,
                              knx_dpt19_datetime_t, knx_dpt26_scene_info_t,
                              knx_dpt27_combined_status_t, knx_dpt232_color_t, knx_dpt251_color_t>;

struct KnxCommunicationObject {
    std::string id;
    std::string name;
    std::string description;
    std::string group_address;
    knx_address_t parsed_group_address = 0;
    KnxDpt datapoint_type;
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
std::string KnxDptName(const KnxDpt& datapoint_type);
bool KnxParseValue(const KnxDpt& datapoint_type, const std::string& text, KnxValue& value);
std::string KnxValueToString(const KnxValue& value);
bool KnxDecodeValue(const KnxDpt& datapoint_type, const uint8_t* data, size_t length,
                    KnxValue& value);
bool KnxEncodeValue(const KnxDpt& datapoint_type, const KnxValue& value,
                    std::vector<uint8_t>& data);