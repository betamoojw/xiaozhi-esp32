#include "knx_object.h"

#include <esp_knx_ip/knx_dpt.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <limits>
#include <sstream>
#include <type_traits>

namespace {

bool ParseAddressPart(const char*& cursor, const char separator, unsigned maximum,
                      unsigned& value) {
    if (*cursor < '0' || *cursor > '9') {
        return false;
    }
    unsigned parsed = 0;
    while (*cursor >= '0' && *cursor <= '9') {
        parsed = parsed * 10 + static_cast<unsigned>(*cursor - '0');
        if (parsed > maximum) {
            return false;
        }
        ++cursor;
    }
    if (separator != '\0') {
        if (*cursor != separator) {
            return false;
        }
        ++cursor;
    } else if (*cursor != '\0') {
        return false;
    }
    value = parsed;
    return true;
}

template <typename T>
bool ParseInteger(const std::string& text, T& value) {
    static_assert(std::is_integral_v<T> && !std::is_same_v<T, bool>);
    if (text.empty()) {
        return false;
    }
    T parsed = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed);
    if (result.ec != std::errc() || result.ptr != text.data() + text.size()) {
        return false;
    }
    value = parsed;
    return true;
}

bool ParseFloat(const std::string& text, float& value) {
    if (text.empty() || text.find_first_not_of("0123456789+-.eE") != std::string::npos) {
        return false;
    }
    errno = 0;
    char* end = nullptr;
    const float parsed = std::strtof(text.c_str(), &end);
    if (errno == ERANGE || end != text.c_str() + text.size() || !std::isfinite(parsed)) {
        return false;
    }
    value = parsed;
    return true;
}

bool IsSupportedDpt(uint16_t main) {
    return (main >= 1 && main <= 31) || main == 232 || main == 234 || main == 251;
}

bool IsStringDpt(const KnxDpt& dpt) {
    return dpt.main == 16 || dpt.main == 24 || dpt.main == 28 || dpt.main == 234 ||
           (dpt.main == 4 && dpt.has_subtype && dpt.subtype == 1);
}

bool IsFloatDpt(const KnxDpt& dpt) {
    return dpt.main == 9 || dpt.main == 14 ||
           (dpt.main == 5 && dpt.has_subtype && (dpt.subtype == 1 || dpt.subtype == 3));
}

size_t WireSize(const KnxDpt& dpt) {
    const auto main = dpt.main;
    if (dpt.has_subtype && main == 30) return 3;
    if (dpt.has_subtype && main == 31) return 1;
    if (main <= 6 || main == 17 || main == 18 || main == 20 || main == 21 || main == 23 || (main >= 25 && main <= 26) ||
        main == 30)
        return 1;
    if (main == 7 || main == 8 || main == 9 || main == 22 || main == 234)
        return 2;
    if (main == 10 || main == 11 || main == 31 || main == 232)
        return 3;
    if ((main >= 12 && main <= 15) || main == 27)
        return 4;
    if (main == 251)
        return 6;
    if (main == 19 || main == 29)
        return 8;
    if (main == 16)
        return 14;
    return 0;
}

size_t PayloadOffset(const KnxDpt& dpt) {
    return dpt.main <= 3 || (dpt.has_subtype && (dpt.main == 23 || dpt.main == 31)) ? 0 : 1;
}

const uint8_t* Payload(const KnxDpt& dpt, const uint8_t* data, size_t length,
                       size_t& payload_length) {
    const size_t offset = PayloadOffset(dpt);
    if (length <= offset)
        return nullptr;
    payload_length = length - offset;
    return data + offset;
}

template <typename T, typename Decode>
bool DecodeScalar(const uint8_t* data, size_t length, Decode decode, KnxValue& value) {
    T decoded{};
    if (!decode(data, length, &decoded))
        return false;
    value = decoded;
    return true;
}

template <typename T, typename Encode>
bool EncodeScalar(const KnxValue& value, uint8_t* data, size_t length, Encode encode) {
    const auto* typed = std::get_if<T>(&value);
    return typed != nullptr && encode(*typed, data, length);
}

template <typename T, typename Decode>
bool DecodeStruct(const uint8_t* data, size_t length, Decode decode, KnxValue& value) {
    T decoded{};
    if (!decode(data, length, &decoded))
        return false;
    value = decoded;
    return true;
}

template <typename T, typename Encode>
bool EncodeStruct(const KnxValue& value, uint8_t* data, size_t length, Encode encode) {
    const auto* typed = std::get_if<T>(&value);
    return typed != nullptr && encode(typed, data, length);
}

bool ParseFields(const std::string& text, std::vector<std::string>& fields) {
    fields.clear();
    size_t start = 0;
    while (start <= text.size()) {
        const size_t end = text.find(',', start);
        fields.push_back(text.substr(start, end == std::string::npos ? end : end - start));
        if (end == std::string::npos)
            break;
        start = end + 1;
    }
    return !fields.empty();
}

bool ParseBool(const std::string& text, bool& value) {
    if (text == "true" || text == "1" || text == "on") {
        value = true;
        return true;
    }
    if (text == "false" || text == "0" || text == "off") {
        value = false;
        return true;
    }
    return false;
}

template <typename T>
void AppendNumber(std::ostringstream& output, const char* name, T value, bool& first) {
    if (!first)
        output << ',';
    output << name << '=' << +value;
    first = false;
}

void AppendBool(std::ostringstream& output, const char* name, bool value, bool& first) {
    if (!first)
        output << ',';
    output << name << '=' << (value ? "true" : "false");
    first = false;
}

}  // namespace

bool KnxParseGroupAddress(const std::string& text, knx_address_t& address) {
    const char* cursor = text.c_str();
    unsigned main = 0;
    unsigned middle = 0;
    unsigned sub = 0;
    if (!ParseAddressPart(cursor, '/', 31, main) || !ParseAddressPart(cursor, '/', 7, middle) ||
        !ParseAddressPart(cursor, '\0', 255, sub)) {
        return false;
    }
    address = knx_group_address(main, middle, sub);
    return true;
}

std::string KnxFormatGroupAddress(knx_address_t address) {
    return std::to_string(knx_group_main(address)) + "/" +
           std::to_string(knx_group_middle(address)) + "/" + std::to_string(knx_group_sub(address));
}

bool KnxParsePhysicalAddress(const std::string& text, knx_address_t& address) {
    const char* cursor = text.c_str();
    unsigned area = 0;
    unsigned line = 0;
    unsigned member = 0;
    if (!ParseAddressPart(cursor, '.', 15, area) || !ParseAddressPart(cursor, '.', 15, line) ||
        !ParseAddressPart(cursor, '\0', 255, member)) {
        return false;
    }
    address = knx_physical_address(area, line, member);
    return true;
}

std::string KnxFormatPhysicalAddress(knx_address_t address) {
    return std::to_string(knx_physical_area(address)) + "." +
           std::to_string(knx_physical_line(address)) + "." +
           std::to_string(knx_physical_member(address));
}

bool KnxParseDpt(const std::string& text, KnxDpt& datapoint_type) {
    std::string value = text;
    if (value.rfind("DPT-", 0) == 0 || value.rfind("dpt-", 0) == 0) {
        value.erase(0, 4);
    }
    const size_t separator = value.find('.');
    const std::string main_text = value.substr(0, separator);
    uint16_t main = 0;
    if (!ParseInteger(main_text, main) || !IsSupportedDpt(main))
        return false;
    KnxDpt parsed{main, 0, false};
    if (separator != std::string::npos) {
        const std::string subtype_text = value.substr(separator + 1);
        if (subtype_text.empty() || subtype_text.size() > 5 ||
            !ParseInteger(subtype_text, parsed.subtype))
            return false;
        parsed.has_subtype = true;
    }
    if (!KnxValidateDpt(parsed)) return false;
    datapoint_type = parsed;
    return true;
}

std::string KnxDptName(const KnxDpt& datapoint_type) {
    std::ostringstream output;
    output << "DPT-" << datapoint_type.main;
    if (datapoint_type.has_subtype) {
        output << '.' << std::setw(3) << std::setfill('0') << datapoint_type.subtype;
    }
    return output.str();
}

static bool ParseValueUnchecked(const KnxDpt& dpt, const std::string& text, KnxValue& value) {
    if (text == "invalid" && dpt.has_subtype &&
        (dpt.main == 9 || (dpt.main == 8 && dpt.subtype == 10) ||
         (dpt.main == 20 && dpt.subtype == 1200))) {
        value = std::monostate{};
        return true;
    }
    if (dpt.main == 1) {
        bool parsed = false;
        if (!ParseBool(text, parsed))
            return false;
        value = parsed;
        return true;
    }
    if (IsStringDpt(dpt)) {
        value = text;
        return true;
    }
    if (IsFloatDpt(dpt)) {
        float parsed = 0;
        if (!ParseFloat(text, parsed))
            return false;
        value = parsed;
        return true;
    }
    if (dpt.main == 6) {
        int8_t parsed{};
        if (!ParseInteger(text, parsed))
            return false;
        value = parsed;
        return true;
    }
    if (dpt.main == 8) {
        int16_t parsed{};
        if (!ParseInteger(text, parsed))
            return false;
        value = parsed;
        return true;
    }
    if (dpt.main == 13) {
        int32_t parsed{};
        if (!ParseInteger(text, parsed))
            return false;
        value = parsed;
        return true;
    }
    if (dpt.main == 29) {
        int64_t parsed{};
        if (!ParseInteger(text, parsed))
            return false;
        value = parsed;
        return true;
    }
    if (dpt.main == 7 || dpt.main == 22) {
        uint16_t parsed{};
        if (!ParseInteger(text, parsed))
            return false;
        value = parsed;
        return true;
    }
    if (dpt.main == 12 || dpt.main == 15 || (dpt.main == 31 && !dpt.has_subtype) || (dpt.main == 30 && dpt.has_subtype)) {
        uint32_t parsed{};
        if (!ParseInteger(text, parsed))
            return false;
        value = parsed;
        return true;
    }
    if (dpt.main == 4 || dpt.main == 5 || dpt.main == 17 || dpt.main == 20 || dpt.main == 21 ||
        dpt.main == 23 || dpt.main == 25 || (dpt.main == 30 && !dpt.has_subtype) || (dpt.main == 31 && dpt.has_subtype)) {
        uint8_t parsed{};
        if (!ParseInteger(text, parsed))
            return false;
        value = parsed;
        return true;
    }

    std::vector<std::string> fields;
    ParseFields(text, fields);
    auto parse_u8 = [&fields](size_t index, uint8_t& parsed) {
        return index < fields.size() && ParseInteger(fields[index], parsed);
    };
    auto parse_u16 = [&fields](size_t index, uint16_t& parsed) {
        return index < fields.size() && ParseInteger(fields[index], parsed);
    };
    auto parse_bool = [&fields](size_t index, bool& parsed) {
        return index < fields.size() && ParseBool(fields[index], parsed);
    };
    switch (dpt.main) {
        case 2: {
            knx_dpt2_control_t parsed{};
            if (fields.size() != 2 || !parse_bool(0, parsed.control) ||
                !parse_bool(1, parsed.value))
                return false;
            value = parsed;
            return true;
        }
        case 3: {
            knx_dpt3_control_t parsed{};
            if (fields.size() != 2 || !parse_bool(0, parsed.control) ||
                !parse_u8(1, parsed.step_code))
                return false;
            value = parsed;
            return true;
        }
        case 10: {
            knx_dpt10_time_t parsed{};
            if (fields.size() != 4 || !parse_u8(0, parsed.weekday) || !parse_u8(1, parsed.hour) ||
                !parse_u8(2, parsed.minute) || !parse_u8(3, parsed.second))
                return false;
            value = parsed;
            return true;
        }
        case 11: {
            knx_dpt11_date_t parsed{};
            if (fields.size() != 3 || !parse_u8(0, parsed.day) || !parse_u8(1, parsed.month) ||
                !parse_u8(2, parsed.year))
                return false;
            value = parsed;
            return true;
        }
        case 18: {
            knx_dpt18_scene_control_t parsed{};
            if (fields.size() != 2 || !parse_bool(0, parsed.learn) ||
                !parse_u8(1, parsed.scene_number))
                return false;
            value = parsed;
            return true;
        }
        case 19: {
            KnxDateTime parsed{};
            if ((fields.size() != 15 && fields.size() != 16) || !parse_u8(0, parsed.year) || !parse_u8(1, parsed.month) ||
                !parse_u8(2, parsed.day) || !parse_u8(3, parsed.weekday) ||
                !parse_u8(4, parsed.hour) || !parse_u8(5, parsed.minute) ||
                !parse_u8(6, parsed.second) || !parse_bool(7, parsed.fault) ||
                !parse_bool(8, parsed.working_day) || !parse_bool(9, parsed.working_day_valid) ||
                !parse_bool(10, parsed.date_valid) || !parse_bool(11, parsed.weekday_valid) ||
                !parse_bool(12, parsed.time_valid) ||
                !parse_bool(13, parsed.daylight_saving_time) ||
                !parse_bool(14, parsed.clock_quality) ||
                (fields.size() == 16 && !parse_bool(15, parsed.year_valid)))
                return false;
            if (fields.size() == 15) parsed.year_valid = parsed.date_valid;
            value = parsed;
            return true;
        }
        case 26: {
            knx_dpt26_scene_info_t parsed{};
            if (fields.size() != 2 || !parse_bool(0, parsed.active) ||
                !parse_u8(1, parsed.scene_number))
                return false;
            value = parsed;
            return true;
        }
        case 27: {
            knx_dpt27_combined_status_t parsed{};
            if (fields.size() != 2 || !parse_u16(0, parsed.value) || !parse_u16(1, parsed.mask))
                return false;
            value = parsed;
            return true;
        }
        case 232: {
            knx_dpt232_color_t parsed{};
            if (fields.size() != 3 || !parse_u8(0, parsed.red) || !parse_u8(1, parsed.green) ||
                !parse_u8(2, parsed.blue))
                return false;
            value = parsed;
            return true;
        }
        case 251: {
            knx_dpt251_color_t parsed{};
            if (fields.size() != 5 || !parse_u8(0, parsed.red) || !parse_u8(1, parsed.green) ||
                !parse_u8(2, parsed.blue) || !parse_u8(3, parsed.white) ||
                !parse_u8(4, parsed.valid_channels))
                return false;
            value = parsed;
            return true;
        }
        default:
            return false;
    }
}

std::string KnxValueToString(const KnxValue& value) {
    return std::visit(
        [](const auto& item) {
            using Type = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<Type, std::monostate>) {
                return std::string("invalid");
            } else if constexpr (std::is_same_v<Type, bool>) {
                return std::string(item ? "true" : "false");
            } else if constexpr (std::is_arithmetic_v<Type>) {
                return std::to_string(item);
            } else if constexpr (std::is_same_v<Type, std::string>) {
                return item;
            } else {
                std::ostringstream output;
                bool first = true;
                if constexpr (std::is_same_v<Type, knx_dpt2_control_t>) {
                    AppendBool(output, "control", item.control, first);
                    AppendBool(output, "value", item.value, first);
                } else if constexpr (std::is_same_v<Type, knx_dpt3_control_t>) {
                    AppendBool(output, "control", item.control, first);
                    AppendNumber(output, "step_code", item.step_code, first);
                } else if constexpr (std::is_same_v<Type, knx_dpt10_time_t>) {
                    AppendNumber(output, "weekday", item.weekday, first);
                    AppendNumber(output, "hour", item.hour, first);
                    AppendNumber(output, "minute", item.minute, first);
                    AppendNumber(output, "second", item.second, first);
                } else if constexpr (std::is_same_v<Type, knx_dpt11_date_t>) {
                    AppendNumber(output, "day", item.day, first);
                    AppendNumber(output, "month", item.month, first);
                    AppendNumber(output, "year", item.year, first);
                } else if constexpr (std::is_same_v<Type, knx_dpt18_scene_control_t>) {
                    AppendBool(output, "learn", item.learn, first);
                    AppendNumber(output, "scene_number", item.scene_number, first);
                } else if constexpr (std::is_same_v<Type, KnxDateTime>) {
                    AppendNumber(output, "year", item.year, first);
                    AppendNumber(output, "month", item.month, first);
                    AppendNumber(output, "day", item.day, first);
                    AppendNumber(output, "weekday", item.weekday, first);
                    AppendNumber(output, "hour", item.hour, first);
                    AppendNumber(output, "minute", item.minute, first);
                    AppendNumber(output, "second", item.second, first);
                    AppendBool(output, "fault", item.fault, first);
                    AppendBool(output, "working_day", item.working_day, first);
                    AppendBool(output, "working_day_valid", item.working_day_valid, first);
                    AppendBool(output, "date_valid", item.date_valid, first);
                    AppendBool(output, "weekday_valid", item.weekday_valid, first);
                    AppendBool(output, "time_valid", item.time_valid, first);
                    AppendBool(output, "daylight_saving_time", item.daylight_saving_time, first);
                    AppendBool(output, "clock_quality", item.clock_quality, first);
                    AppendBool(output, "year_valid", item.year_valid, first);
                } else if constexpr (std::is_same_v<Type, knx_dpt26_scene_info_t>) {
                    AppendBool(output, "active", item.active, first);
                    AppendNumber(output, "scene_number", item.scene_number, first);
                } else if constexpr (std::is_same_v<Type, knx_dpt27_combined_status_t>) {
                    AppendNumber(output, "value", item.value, first);
                    AppendNumber(output, "mask", item.mask, first);
                } else if constexpr (std::is_same_v<Type, knx_dpt232_color_t>) {
                    AppendNumber(output, "red", item.red, first);
                    AppendNumber(output, "green", item.green, first);
                    AppendNumber(output, "blue", item.blue, first);
                } else if constexpr (std::is_same_v<Type, knx_dpt251_color_t>) {
                    AppendNumber(output, "red", item.red, first);
                    AppendNumber(output, "green", item.green, first);
                    AppendNumber(output, "blue", item.blue, first);
                    AppendNumber(output, "white", item.white, first);
                    AppendNumber(output, "valid_channels", item.valid_channels, first);
                }
                return output.str();
            }
        },
        value);
}

static bool DecodeValueUnchecked(const KnxDpt& dpt, const uint8_t* data, size_t length, KnxValue& value) {
    if (data == nullptr || length == 0) {
        return false;
    }
    size_t payload_length = 0;
    const uint8_t* payload = Payload(dpt, data, length, payload_length);
    if (payload == nullptr)
        return false;
    if (dpt.has_subtype) {
        if (dpt.main == 30) return DecodeScalar<uint32_t>(payload, payload_length, knx_dpt31_decode, value);
        if (dpt.main == 31) { value=payload[0]; return true; }
        if (dpt.main == 234) {
            std::string text(reinterpret_cast<const char*>(payload), 2);
            for (char& c : text) {
                if (dpt.subtype==1 && c>='A' && c<='Z') c+='a'-'A';
                if (dpt.subtype==2 && c>='a' && c<='z') c-='a'-'A';
            }
            value=text; return true;
        }
        if (dpt.main == 26) {
            knx_dpt26_scene_info_t p{};
            if (!knx_dpt26_decode(payload,payload_length,&p)) return false;
            p.active=!p.active; value=p; return true;
        }
        if (dpt.main == 27) {
            knx_dpt27_combined_status_t p{};
            if (!knx_dpt27_decode(payload,payload_length,&p)) return false;
            std::swap(p.value,p.mask); value=p; return true;
        }
    }
    switch (dpt.main) {
        case 1:
            return DecodeScalar<bool>(payload, payload_length, knx_dpt1_decode, value);
        case 2:
            return DecodeStruct<knx_dpt2_control_t>(payload, payload_length, knx_dpt2_decode,
                                                    value);
        case 3:
            return DecodeStruct<knx_dpt3_control_t>(payload, payload_length, knx_dpt3_decode,
                                                    value);
        case 4: {
            if (dpt.has_subtype && dpt.subtype == 1) {
                char decoded{};
                if (!knx_dpt4_ascii_decode(payload, payload_length, &decoded))
                    return false;
                value = std::string(1, decoded);
                return true;
            }
            return DecodeScalar<uint8_t>(payload, payload_length, knx_dpt4_decode, value);
        }
        case 5:
            if (dpt.has_subtype && dpt.subtype == 1)
                return DecodeScalar<float>(payload, payload_length, knx_dpt5_scaling_decode, value);
            if (dpt.has_subtype && dpt.subtype == 3)
                return DecodeScalar<float>(payload, payload_length, knx_dpt5_angle_decode, value);
            return DecodeScalar<uint8_t>(payload, payload_length, knx_dpt5_decode, value);
        case 6:
            return DecodeScalar<int8_t>(payload, payload_length, knx_dpt6_decode, value);
        case 7:
            return DecodeScalar<uint16_t>(payload, payload_length, knx_dpt7_decode, value);
        case 8:
            return DecodeScalar<int16_t>(payload, payload_length, knx_dpt8_decode, value);
        case 9:
            return DecodeScalar<float>(payload, payload_length, knx_dpt9_decode, value);
        case 10:
            return DecodeStruct<knx_dpt10_time_t>(payload, payload_length, knx_dpt10_decode, value);
        case 11:
            return DecodeStruct<knx_dpt11_date_t>(payload, payload_length, knx_dpt11_decode, value);
        case 12:
            return DecodeScalar<uint32_t>(payload, payload_length, knx_dpt12_decode, value);
        case 13:
            return DecodeScalar<int32_t>(payload, payload_length, knx_dpt13_decode, value);
        case 14:
            return DecodeScalar<float>(payload, payload_length, knx_dpt14_decode, value);
        case 15:
            return DecodeScalar<uint32_t>(payload, payload_length, knx_dpt15_decode, value);
        case 16: {
            char decoded[15]{};
            if (!knx_dpt16_decode(payload, payload_length, decoded, sizeof(decoded)))
                return false;
            value = std::string(decoded);
            return true;
        }
        case 17:
            return DecodeScalar<uint8_t>(payload, payload_length, knx_dpt17_decode, value);
        case 18:
            return DecodeStruct<knx_dpt18_scene_control_t>(payload, payload_length,
                                                           knx_dpt18_decode, value);
        case 19: {
            if ((payload[1]&0xf0) || (payload[2]&0xe0) || (payload[4]&0xc0) ||
                (payload[5]&0xc0) || (payload[7]&0x7f)) return false;
            value = KnxDateTime{payload[0], payload[1], payload[2],
                static_cast<uint8_t>(payload[3]>>5), static_cast<uint8_t>(payload[3]&31),
                payload[4], payload[5], (payload[6]&128)!=0, (payload[6]&64)!=0,
                (payload[6]&32)==0, (payload[6]&8)==0, (payload[6]&4)==0,
                (payload[6]&2)==0, (payload[6]&1)!=0, (payload[7]&128)!=0,
                (payload[6]&16)==0};
            return true;
        }
        case 20:
            return DecodeScalar<uint8_t>(payload, payload_length, knx_dpt20_decode, value);
        case 21:
            return DecodeScalar<uint8_t>(payload, payload_length, knx_dpt21_decode, value);
        case 22:
            return DecodeScalar<uint16_t>(payload, payload_length, knx_dpt22_decode, value);
        case 23:
            return DecodeScalar<uint8_t>(payload, payload_length, knx_dpt23_decode, value);
        case 24:
        case 28: {
            std::vector<char> decoded(payload_length + 1);
            const bool ok =
                dpt.main == 24
                    ? knx_dpt24_decode(payload, payload_length, decoded.data(), decoded.size())
                    : knx_dpt28_decode(payload, payload_length, decoded.data(), decoded.size());
            if (!ok)
                return false;
            value = std::string(decoded.data());
            return true;
        }
        case 25:
            return DecodeScalar<uint8_t>(payload, payload_length, knx_dpt25_decode, value);
        case 26:
            return DecodeStruct<knx_dpt26_scene_info_t>(payload, payload_length, knx_dpt26_decode,
                                                        value);
        case 27:
            return DecodeStruct<knx_dpt27_combined_status_t>(payload, payload_length,
                                                             knx_dpt27_decode, value);
        case 29:
            return DecodeScalar<int64_t>(payload, payload_length, knx_dpt29_decode, value);
        case 30:
            return DecodeScalar<uint8_t>(payload, payload_length, knx_dpt30_decode, value);
        case 31:
            return DecodeScalar<uint32_t>(payload, payload_length, knx_dpt31_decode, value);
        case 232:
            return DecodeStruct<knx_dpt232_color_t>(payload, payload_length, knx_dpt232_decode,
                                                    value);
        case 234: {
            char decoded[3]{};
            if (!knx_dpt234_decode(payload, payload_length, decoded))
                return false;
            value = std::string(decoded);
            return true;
        }
        case 251:
            return DecodeStruct<knx_dpt251_color_t>(payload, payload_length, knx_dpt251_decode,
                                                    value);
        default:
            return false;
    }
}

static bool EncodeValueUnchecked(const KnxDpt& dpt, const KnxValue& value, std::vector<uint8_t>& data) {
    const size_t offset = PayloadOffset(dpt);
    size_t wire_size = WireSize(dpt);
    if (dpt.main == 24 || dpt.main == 28) {
        const auto* text = std::get_if<std::string>(&value);
        // One APDU byte plus the NUL-terminated text must fit the component telegram buffer.
        if (text == nullptr || text->size() > 253)
            return false;
        wire_size = text->size() + 1;
    }
    if (wire_size == 0)
        return false;
    data.assign(offset + wire_size, 0);
    uint8_t* payload = data.data() + offset;
    bool encoded = false;
    if (std::holds_alternative<std::monostate>(value)) {
        if (dpt.main==20) payload[0]=255;
        else { payload[0]=0x7f; payload[1]=0xff; }
        return true;
    }
    if (dpt.has_subtype) {
        if (dpt.main==30) return knx_dpt31_encode(std::get<uint32_t>(value),payload,wire_size);
        if (dpt.main==31) { payload[0]=std::get<uint8_t>(value); return true; }
        if (dpt.main==234) {
            const auto& text=std::get<std::string>(value);
            for (unsigned i=0;i<2;++i) {
                char c=text[i];
                if (dpt.subtype==1 && c>='A' && c<='Z') c+='a'-'A';
                if (dpt.subtype==2 && c>='a' && c<='z') c-='a'-'A';
                payload[i]=static_cast<uint8_t>(c);
            }
            return true;
        }
        if (dpt.main==26) {
            auto p=std::get<knx_dpt26_scene_info_t>(value); p.active=!p.active;
            return knx_dpt26_encode(&p,payload,wire_size);
        }
        if (dpt.main==27) {
            auto p=std::get<knx_dpt27_combined_status_t>(value); std::swap(p.value,p.mask);
            return knx_dpt27_encode(&p,payload,wire_size);
        }
        if (dpt.main==9) {
            double mantissa=std::get<float>(value)*100.0;
            unsigned exponent=0;
            while ((std::round(mantissa)<-2048 || std::round(mantissa)>2047) && exponent<15) {
                mantissa/=2; ++exponent;
            }
            const int m=static_cast<int>(std::round(mantissa));
            if (m<-2048 || m>2047 || (m==2047 && exponent==15)) return false;
            payload[0]=(m<0 ? 0x80 : 0) | (exponent<<3) | ((m>>8)&7);
            payload[1]=static_cast<uint8_t>(m);
            return true;
        }
    }
    switch (dpt.main) {
        case 1:
            encoded = EncodeScalar<bool>(value, payload, wire_size, knx_dpt1_encode);
            break;
        case 2:
            encoded = EncodeStruct<knx_dpt2_control_t>(value, payload, wire_size, knx_dpt2_encode);
            break;
        case 3:
            encoded = EncodeStruct<knx_dpt3_control_t>(value, payload, wire_size, knx_dpt3_encode);
            break;
        case 4:
            if (dpt.has_subtype && dpt.subtype == 1) {
                const auto* text = std::get_if<std::string>(&value);
                encoded = text != nullptr && text->size() == 1 &&
                          knx_dpt4_ascii_encode((*text)[0], payload, wire_size);
            } else
                encoded = EncodeScalar<uint8_t>(value, payload, wire_size, knx_dpt4_encode);
            break;
        case 5:
            if (dpt.has_subtype && dpt.subtype == 1)
                encoded = EncodeScalar<float>(value, payload, wire_size, knx_dpt5_scaling_encode);
            else if (dpt.has_subtype && dpt.subtype == 3)
                encoded = EncodeScalar<float>(value, payload, wire_size, knx_dpt5_angle_encode);
            else
                encoded = EncodeScalar<uint8_t>(value, payload, wire_size, knx_dpt5_encode);
            break;
        case 6:
            encoded = EncodeScalar<int8_t>(value, payload, wire_size, knx_dpt6_encode);
            break;
        case 7:
            encoded = EncodeScalar<uint16_t>(value, payload, wire_size, knx_dpt7_encode);
            break;
        case 8:
            encoded = EncodeScalar<int16_t>(value, payload, wire_size, knx_dpt8_encode);
            break;
        case 9:
            encoded = EncodeScalar<float>(value, payload, wire_size, knx_dpt9_encode);
            break;
        case 10:
            encoded = EncodeStruct<knx_dpt10_time_t>(value, payload, wire_size, knx_dpt10_encode);
            break;
        case 11:
            encoded = EncodeStruct<knx_dpt11_date_t>(value, payload, wire_size, knx_dpt11_encode);
            break;
        case 12:
            encoded = EncodeScalar<uint32_t>(value, payload, wire_size, knx_dpt12_encode);
            break;
        case 13:
            encoded = EncodeScalar<int32_t>(value, payload, wire_size, knx_dpt13_encode);
            break;
        case 14:
            encoded = EncodeScalar<float>(value, payload, wire_size, knx_dpt14_encode);
            break;
        case 15:
            encoded = EncodeScalar<uint32_t>(value, payload, wire_size, knx_dpt15_encode);
            break;
        case 16: {
            const auto* text = std::get_if<std::string>(&value);
            encoded = text != nullptr && knx_dpt16_encode(text->c_str(), payload, wire_size);
            break;
        }
        case 17:
            encoded = EncodeScalar<uint8_t>(value, payload, wire_size, knx_dpt17_encode);
            break;
        case 18:
            encoded = EncodeStruct<knx_dpt18_scene_control_t>(value, payload, wire_size,
                                                              knx_dpt18_encode);
            break;
        case 19: {
            const auto& p=std::get<KnxDateTime>(value);
            payload[0]=p.year; payload[1]=p.month; payload[2]=p.day;
            payload[3]=(p.weekday<<5)|p.hour; payload[4]=p.minute; payload[5]=p.second;
            payload[6]=(p.fault?128:0)|(p.working_day?64:0)|(!p.working_day_valid?32:0)|
                (!p.year_valid?16:0)|(!p.date_valid?8:0)|(!p.weekday_valid?4:0)|
                (!p.time_valid?2:0)|(p.daylight_saving_time?1:0);
            payload[7]=p.clock_quality?128:0;
            encoded=true;
            break;
        }
        case 20:
            encoded = EncodeScalar<uint8_t>(value, payload, wire_size, knx_dpt20_encode);
            break;
        case 21:
            encoded = EncodeScalar<uint8_t>(value, payload, wire_size, knx_dpt21_encode);
            break;
        case 22:
            encoded = EncodeScalar<uint16_t>(value, payload, wire_size, knx_dpt22_encode);
            break;
        case 23:
            encoded = EncodeScalar<uint8_t>(value, payload, wire_size, knx_dpt23_encode);
            break;
        case 24:
        case 28: {
            const auto* text = std::get_if<std::string>(&value);
            size_t encoded_length = 0;
            encoded = text != nullptr &&
                      (dpt.main == 24
                           ? knx_dpt24_encode(text->c_str(), payload, wire_size, &encoded_length)
                           : knx_dpt28_encode(text->c_str(), payload, wire_size, &encoded_length));
            if (encoded)
                data.resize(offset + encoded_length);
            break;
        }
        case 25:
            encoded = EncodeScalar<uint8_t>(value, payload, wire_size, knx_dpt25_encode);
            break;
        case 26:
            encoded =
                EncodeStruct<knx_dpt26_scene_info_t>(value, payload, wire_size, knx_dpt26_encode);
            break;
        case 27:
            encoded = EncodeStruct<knx_dpt27_combined_status_t>(value, payload, wire_size,
                                                                knx_dpt27_encode);
            break;
        case 29:
            encoded = EncodeScalar<int64_t>(value, payload, wire_size, knx_dpt29_encode);
            break;
        case 30:
            encoded = EncodeScalar<uint8_t>(value, payload, wire_size, knx_dpt30_encode);
            break;
        case 31:
            encoded = EncodeScalar<uint32_t>(value, payload, wire_size, knx_dpt31_encode);
            break;
        case 232:
            encoded =
                EncodeStruct<knx_dpt232_color_t>(value, payload, wire_size, knx_dpt232_encode);
            break;
        case 234: {
            const auto* text = std::get_if<std::string>(&value);
            encoded = text != nullptr && text->size() == 2 &&
                      knx_dpt234_encode(text->c_str(), payload, wire_size);
            break;
        }
        case 251:
            encoded =
                EncodeStruct<knx_dpt251_color_t>(value, payload, wire_size, knx_dpt251_encode);
            break;
        default:
            encoded = false;
            break;
    }
    if (!encoded)
        data.clear();
    return encoded;
}
bool KnxParseValue(const KnxDpt& dpt, const std::string& text, KnxValue& value) {
    KnxValue parsed;
    if (!KnxValidateDpt(dpt) || !ParseValueUnchecked(dpt,text,parsed) ||
        !KnxValidateValue(dpt,parsed)) return false;
    // Also check quantization cannot turn a numeric value into an invalid sentinel.
    std::vector<uint8_t> encoded;
    if (!KnxEncodeValue(dpt,parsed,encoded)) return false;
    value=std::move(parsed);
    return true;
}

bool KnxEncodeValue(const KnxDpt& dpt, const KnxValue& value, std::vector<uint8_t>& data) {
    // Failure always clears encoded output, including early validation failures.
    data.clear();
    if (!KnxValidateValue(dpt,value) || !EncodeValueUnchecked(dpt,value,data)) {
        data.clear(); return false;
    }
    return true;
}

bool KnxDecodeValue(const KnxDpt& dpt, const uint8_t* data, size_t length, KnxValue& value) {
    if (!KnxValidateDpt(dpt) || !data || length==0 || length>255) return false;
    const size_t offset=PayloadOffset(dpt);
    if (length<=offset) return false;
    const size_t size=length-offset;
    const auto* p=data+offset;
    if (dpt.main==24 || dpt.main==28) {
        if (p[size-1]!=0 || std::memchr(p,0,size-1)) return false;
    } else if (size!=WireSize(dpt)) return false;
    if (dpt.main==16) {
        bool ended=false;
        for (size_t i=0;i<size;++i) {
            if (ended && p[i]!=0) return false;
            if (p[i]==0) ended=true;
        }
    }
    if (dpt.has_subtype && ((dpt.main==9 && p[0]==0x7f && p[1]==0xff) ||
        (dpt.main==8 && dpt.subtype==10 && p[0]==0x7f && p[1]==0xff) ||
        (dpt.main==20 && dpt.subtype==1200 && p[0]==255))) return false;
    KnxValue decoded;
    if (!DecodeValueUnchecked(dpt,data,length,decoded) || !KnxValidateValue(dpt,decoded)) return false;
    if (dpt.main==19 && std::get<KnxDateTime>(decoded).fault) return false;
    value=std::move(decoded);
    return true;
}
