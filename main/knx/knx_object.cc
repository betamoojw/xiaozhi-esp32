#include "knx_object.h"

#include <esp_knx_ip/knx_dpt.h>

#include <charconv>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <limits>
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
    if (text.empty()) {
        return false;
    }
    errno = 0;
    char* end = nullptr;
    const float parsed = std::strtof(text.c_str(), &end);
    if (errno == ERANGE || end != text.c_str() + text.size() ||
        !std::isfinite(parsed)) {
        return false;
    }
    value = parsed;
    return true;
}

}  // namespace

bool KnxParseGroupAddress(const std::string& text, knx_address_t& address) {
    const char* cursor = text.c_str();
    unsigned main = 0;
    unsigned middle = 0;
    unsigned sub = 0;
    if (!ParseAddressPart(cursor, '/', 31, main) ||
        !ParseAddressPart(cursor, '/', 7, middle) ||
        !ParseAddressPart(cursor, '\0', 255, sub)) {
        return false;
    }
    address = knx_group_address(main, middle, sub);
    return true;
}

std::string KnxFormatGroupAddress(knx_address_t address) {
    return std::to_string(knx_group_main(address)) + "/" +
           std::to_string(knx_group_middle(address)) + "/" +
           std::to_string(knx_group_sub(address));
}

bool KnxParsePhysicalAddress(const std::string& text, knx_address_t& address) {
    const char* cursor = text.c_str();
    unsigned area = 0;
    unsigned line = 0;
    unsigned member = 0;
    if (!ParseAddressPart(cursor, '.', 15, area) ||
        !ParseAddressPart(cursor, '.', 15, line) ||
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
    const auto subtype = value.find('.');
    if (subtype != std::string::npos) {
        value.resize(subtype);
    }
    int family = 0;
    if (!ParseInteger(value, family)) {
        return false;
    }
    switch (family) {
        case 1: datapoint_type = KnxDpt::kBoolean; return true;
        case 5: datapoint_type = KnxDpt::kUnsigned8; return true;
        case 7: datapoint_type = KnxDpt::kUnsigned16; return true;
        case 9: datapoint_type = KnxDpt::kFloat16; return true;
        case 12: datapoint_type = KnxDpt::kUnsigned32; return true;
        case 13: datapoint_type = KnxDpt::kSigned32; return true;
        case 14: datapoint_type = KnxDpt::kFloat32; return true;
        default: return false;
    }
}

const char* KnxDptName(KnxDpt datapoint_type) {
    switch (datapoint_type) {
        case KnxDpt::kBoolean: return "DPT-1";
        case KnxDpt::kUnsigned8: return "DPT-5";
        case KnxDpt::kUnsigned16: return "DPT-7";
        case KnxDpt::kFloat16: return "DPT-9";
        case KnxDpt::kUnsigned32: return "DPT-12";
        case KnxDpt::kSigned32: return "DPT-13";
        case KnxDpt::kFloat32: return "DPT-14";
    }
    return "unknown";
}

bool KnxParseValue(KnxDpt datapoint_type, const std::string& text, KnxValue& value) {
    switch (datapoint_type) {
        case KnxDpt::kBoolean:
            if (text == "true" || text == "1" || text == "on") {
                value = true;
                return true;
            }
            if (text == "false" || text == "0" || text == "off") {
                value = false;
                return true;
            }
            return false;
        case KnxDpt::kUnsigned8: {
            uint8_t parsed = 0;
            if (!ParseInteger(text, parsed)) return false;
            value = parsed;
            return true;
        }
        case KnxDpt::kUnsigned16: {
            uint16_t parsed = 0;
            if (!ParseInteger(text, parsed)) return false;
            value = parsed;
            return true;
        }
        case KnxDpt::kUnsigned32: {
            uint32_t parsed = 0;
            if (!ParseInteger(text, parsed)) return false;
            value = parsed;
            return true;
        }
        case KnxDpt::kSigned32: {
            int32_t parsed = 0;
            if (!ParseInteger(text, parsed)) return false;
            value = parsed;
            return true;
        }
        case KnxDpt::kFloat16:
        case KnxDpt::kFloat32: {
            float parsed = 0;
            if (!ParseFloat(text, parsed)) return false;
            value = parsed;
            return true;
        }
    }
    return false;
}

std::string KnxValueToString(const KnxValue& value) {
    return std::visit([](const auto& item) {
        using Type = std::decay_t<decltype(item)>;
        if constexpr (std::is_same_v<Type, bool>) {
            return std::string(item ? "true" : "false");
        } else if constexpr (std::is_same_v<Type, uint8_t>) {
            return std::to_string(static_cast<unsigned>(item));
        } else {
            return std::to_string(item);
        }
    }, value);
}

bool KnxDecodeValue(KnxDpt datapoint_type, const uint8_t* data, size_t length,
                    KnxValue& value) {
    if (data == nullptr || length == 0) {
        return false;
    }
    switch (datapoint_type) {
        case KnxDpt::kBoolean: {
            bool decoded = false;
            if (!knx_dpt1_decode(data, length, &decoded)) return false;
            value = decoded;
            return true;
        }
        case KnxDpt::kUnsigned8: {
            uint8_t decoded = 0;
            if (length < 2 || !knx_dpt5_decode(data + 1, length - 1, &decoded)) return false;
            value = decoded;
            return true;
        }
        case KnxDpt::kUnsigned16: {
            uint16_t decoded = 0;
            if (length < 3 || !knx_dpt7_decode(data + 1, length - 1, &decoded)) return false;
            value = decoded;
            return true;
        }
        case KnxDpt::kFloat16: {
            float decoded = 0;
            if (length < 3 || !knx_dpt9_decode(data + 1, length - 1, &decoded)) return false;
            value = decoded;
            return true;
        }
        case KnxDpt::kUnsigned32: {
            uint32_t decoded = 0;
            if (length < 5 || !knx_dpt12_decode(data + 1, length - 1, &decoded)) return false;
            value = decoded;
            return true;
        }
        case KnxDpt::kSigned32: {
            int32_t decoded = 0;
            if (length < 5 || !knx_dpt13_decode(data + 1, length - 1, &decoded)) return false;
            value = decoded;
            return true;
        }
        case KnxDpt::kFloat32: {
            float decoded = 0;
            if (length < 5 || !knx_dpt14_decode(data + 1, length - 1, &decoded)) return false;
            value = decoded;
            return true;
        }
    }
    return false;
}

bool KnxEncodeValue(KnxDpt datapoint_type, const KnxValue& value,
                    std::vector<uint8_t>& data) {
    data.assign(datapoint_type == KnxDpt::kBoolean ? 1 :
                (datapoint_type == KnxDpt::kUnsigned8 ? 2 :
                 (datapoint_type == KnxDpt::kUnsigned16 || datapoint_type == KnxDpt::kFloat16 ? 3 : 5)), 0);
    switch (datapoint_type) {
        case KnxDpt::kBoolean: {
            const auto* typed = std::get_if<bool>(&value);
            return typed != nullptr && knx_dpt1_encode(*typed, data.data(), data.size());
        }
        case KnxDpt::kUnsigned8: {
            const auto* typed = std::get_if<uint8_t>(&value);
            return typed != nullptr && knx_dpt5_encode(*typed, data.data() + 1, data.size() - 1);
        }
        case KnxDpt::kUnsigned16: {
            const auto* typed = std::get_if<uint16_t>(&value);
            return typed != nullptr && knx_dpt7_encode(*typed, data.data() + 1, data.size() - 1);
        }
        case KnxDpt::kFloat16: {
            const auto* typed = std::get_if<float>(&value);
            return typed != nullptr && knx_dpt9_encode(*typed, data.data() + 1, data.size() - 1);
        }
        case KnxDpt::kUnsigned32: {
            const auto* typed = std::get_if<uint32_t>(&value);
            return typed != nullptr && knx_dpt12_encode(*typed, data.data() + 1, data.size() - 1);
        }
        case KnxDpt::kSigned32: {
            const auto* typed = std::get_if<int32_t>(&value);
            return typed != nullptr && knx_dpt13_encode(*typed, data.data() + 1, data.size() - 1);
        }
        case KnxDpt::kFloat32: {
            const auto* typed = std::get_if<float>(&value);
            return typed != nullptr && knx_dpt14_encode(*typed, data.data() + 1, data.size() - 1);
        }
    }
    return false;
}