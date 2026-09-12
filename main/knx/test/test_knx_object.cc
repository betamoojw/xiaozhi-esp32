#include "knx_object.h"

#include <cassert>
#include <cmath>
#include <iostream>

namespace {

void TestAddresses() {
    knx_address_t address = 0;
    assert(KnxParseGroupAddress("31/7/255", address));
    assert(KnxFormatGroupAddress(address) == "31/7/255");
    assert(!KnxParseGroupAddress("32/0/0", address));
    assert(!KnxParseGroupAddress("1/8/0", address));
    assert(!KnxParseGroupAddress("1/0/256", address));
    assert(!KnxParseGroupAddress("1//1", address));
    assert(!KnxParseGroupAddress("1/1/1x", address));

    assert(KnxParsePhysicalAddress("15.15.255", address));
    assert(KnxFormatPhysicalAddress(address) == "15.15.255");
    assert(!KnxParsePhysicalAddress("16.0.1", address));
    assert(!KnxParsePhysicalAddress("1.16.1", address));
}

void TestDptSelectionAndValues() {
    KnxDpt datapoint_type;
    assert(KnxParseDpt("DPT-1.001", datapoint_type));
    assert((datapoint_type == KnxDpt{1, 1, true}));
    assert(KnxDptName(datapoint_type) == "DPT-1.001");
    assert(KnxParseDpt("9", datapoint_type));
    assert((datapoint_type == KnxDpt{9, 0, false}));
    assert(KnxParseDpt("DPT-17.001", datapoint_type));
    assert(KnxParseDpt("DPT-20.102", datapoint_type));
    assert(KnxParseDpt("DPT-251.600", datapoint_type));
    assert(!KnxParseDpt("DPT-32.001", datapoint_type));
    assert(!KnxParseDpt("DPT-5.", datapoint_type));
    for (uint16_t family = 1; family <= 31; ++family) {
        assert(KnxParseDpt("DPT-" + std::to_string(family) + ".001", datapoint_type));
    }
    for (uint16_t family : {232, 234, 251}) {
        assert(KnxParseDpt("DPT-" + std::to_string(family) + ".001", datapoint_type));
    }

    KnxValue value;
    assert(KnxParseValue(KnxDpt{1}, "on", value));
    assert(std::get<bool>(value));
    assert(!KnxParseValue(KnxDpt{1}, "yes", value));
    assert(KnxParseValue(KnxDpt{5}, "255", value));
    assert(std::get<uint8_t>(value) == 255);
    assert(!KnxParseValue(KnxDpt{5}, "256", value));
    assert(!KnxParseValue(KnxDpt{7}, "-1", value));
    assert(KnxParseValue(KnxDpt{13}, "-2147483648", value));
    assert(!KnxParseValue(KnxDpt{13}, "2147483648", value));
    assert(!KnxParseValue(KnxDpt{14}, "1e-9999", value));
    assert(!KnxParseValue(KnxDpt{14}, "nan", value));
    assert(KnxParseValue(KnxDpt{5, 1, true}, "50", value));
    assert(std::holds_alternative<float>(value));
}

void CheckRoundTrip(KnxDpt datapoint_type, const KnxValue& expected, float tolerance = 0.0f) {
    std::vector<uint8_t> encoded;
    assert(KnxEncodeValue(datapoint_type, expected, encoded));
    KnxValue decoded;
    assert(KnxDecodeValue(datapoint_type, encoded.data(), encoded.size(), decoded));
    if (std::holds_alternative<float>(expected)) {
        assert(std::fabs(std::get<float>(decoded) - std::get<float>(expected)) <= tolerance);
    } else {
        assert(KnxValueToString(decoded) == KnxValueToString(expected));
    }
}

void TestRoundTrips() {
    CheckRoundTrip(KnxDpt{1}, KnxValue(true));
    CheckRoundTrip(KnxDpt{2}, KnxValue(knx_dpt2_control_t{true, false}));
    CheckRoundTrip(KnxDpt{3}, KnxValue(knx_dpt3_control_t{true, 5}));
    CheckRoundTrip(KnxDpt{4}, KnxValue(uint8_t{65}));
    CheckRoundTrip(KnxDpt{4, 1, true}, KnxValue(std::string("A")));
    CheckRoundTrip(KnxDpt{5}, KnxValue(uint8_t{200}));
    CheckRoundTrip(KnxDpt{5, 1, true}, KnxValue(50.0f), 0.2f);
    CheckRoundTrip(KnxDpt{5, 3, true}, KnxValue(180.0f), 1.5f);
    CheckRoundTrip(KnxDpt{6}, KnxValue(int8_t{-100}));
    CheckRoundTrip(KnxDpt{7}, KnxValue(uint16_t{50000}));
    CheckRoundTrip(KnxDpt{8}, KnxValue(int16_t{-20000}));
    CheckRoundTrip(KnxDpt{9}, KnxValue(21.5f), 0.02f);
    CheckRoundTrip(KnxDpt{10}, KnxValue(knx_dpt10_time_t{3, 12, 34, 56}));
    CheckRoundTrip(KnxDpt{11}, KnxValue(knx_dpt11_date_t{29, 2, 24}));
    CheckRoundTrip(KnxDpt{12}, KnxValue(uint32_t{4000000000U}));
    CheckRoundTrip(KnxDpt{13}, KnxValue(int32_t{-2000000000}));
    CheckRoundTrip(KnxDpt{14}, KnxValue(123.25f));
    CheckRoundTrip(KnxDpt{15}, KnxValue(uint32_t{0x12345678}));
    CheckRoundTrip(KnxDpt{16}, KnxValue(std::string("KNX text")));
    CheckRoundTrip(KnxDpt{17}, KnxValue(uint8_t{42}));
    CheckRoundTrip(KnxDpt{18}, KnxValue(knx_dpt18_scene_control_t{true, 12}));
    CheckRoundTrip(KnxDpt{19}, KnxValue(knx_dpt19_datetime_t{126, 9, 12, 6, 14, 30, 15, false, true,
                                                             true, true, true, true, false, true}));
    CheckRoundTrip(KnxDpt{20}, KnxValue(uint8_t{7}));
    CheckRoundTrip(KnxDpt{21}, KnxValue(uint8_t{0xa5}));
    CheckRoundTrip(KnxDpt{22}, KnxValue(uint16_t{0xa55a}));
    CheckRoundTrip(KnxDpt{23}, KnxValue(uint8_t{3}));
    CheckRoundTrip(KnxDpt{24}, KnxValue(std::string("latin text")));
    CheckRoundTrip(KnxDpt{25}, KnxValue(uint8_t{0xab}));
    CheckRoundTrip(KnxDpt{26}, KnxValue(knx_dpt26_scene_info_t{true, 22}));
    CheckRoundTrip(KnxDpt{27}, KnxValue(knx_dpt27_combined_status_t{0x1234, 0xffff}));
    CheckRoundTrip(KnxDpt{28}, KnxValue(std::string("UTF-8 text")));
    CheckRoundTrip(KnxDpt{29}, KnxValue(int64_t{-123456789012345LL}));
    CheckRoundTrip(KnxDpt{30}, KnxValue(uint8_t{0x5a}));
    CheckRoundTrip(KnxDpt{31}, KnxValue(uint32_t{0xabcdef}));
    CheckRoundTrip(KnxDpt{232}, KnxValue(knx_dpt232_color_t{10, 20, 30}));
    CheckRoundTrip(KnxDpt{234}, KnxValue(std::string("en")));
    CheckRoundTrip(KnxDpt{251}, KnxValue(knx_dpt251_color_t{10, 20, 30, 40, 0x0f}));

    std::vector<uint8_t> encoded;
    assert(KnxEncodeValue(KnxDpt{1}, KnxValue(true), encoded));
    assert(encoded.size() == 1 && encoded[0] == 1);
    assert(KnxEncodeValue(KnxDpt{7}, KnxValue(uint16_t{0x1234}), encoded));
    assert(encoded.size() == 3 && encoded[0] == 0 && encoded[1] == 0x12 && encoded[2] == 0x34);
    assert(!KnxEncodeValue(KnxDpt{7}, KnxValue(true), encoded));
    assert(!KnxEncodeValue(KnxDpt{28}, KnxValue(std::string(254, 'x')), encoded));
}

}  // namespace

int main() {
    TestAddresses();
    TestDptSelectionAndValues();
    TestRoundTrips();
    std::cout << "KNX object tests passed\n";
    return 0;
}