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
    assert(datapoint_type == KnxDpt::kBoolean);
    assert(KnxParseDpt("9", datapoint_type));
    assert(datapoint_type == KnxDpt::kFloat16);
    assert(!KnxParseDpt("DPT-17.001", datapoint_type));
    assert(!KnxParseDpt("DPT-20.102", datapoint_type));

    KnxValue value;
    assert(KnxParseValue(KnxDpt::kBoolean, "on", value));
    assert(std::get<bool>(value));
    assert(!KnxParseValue(KnxDpt::kBoolean, "yes", value));
    assert(KnxParseValue(KnxDpt::kUnsigned8, "255", value));
    assert(std::get<uint8_t>(value) == 255);
    assert(!KnxParseValue(KnxDpt::kUnsigned8, "256", value));
    assert(!KnxParseValue(KnxDpt::kUnsigned16, "-1", value));
    assert(KnxParseValue(KnxDpt::kSigned32, "-2147483648", value));
    assert(!KnxParseValue(KnxDpt::kSigned32, "2147483648", value));
    assert(!KnxParseValue(KnxDpt::kFloat32, "1e-9999", value));
    assert(!KnxParseValue(KnxDpt::kFloat32, "nan", value));
}

void CheckRoundTrip(KnxDpt datapoint_type, const KnxValue& expected,
                    float tolerance = 0.0f) {
    std::vector<uint8_t> encoded;
    assert(KnxEncodeValue(datapoint_type, expected, encoded));
    KnxValue decoded;
    assert(KnxDecodeValue(datapoint_type, encoded.data(), encoded.size(), decoded));
    if (std::holds_alternative<float>(expected)) {
        assert(std::fabs(std::get<float>(decoded) - std::get<float>(expected)) <= tolerance);
    } else {
        assert(decoded == expected);
    }
}

void TestRoundTrips() {
    CheckRoundTrip(KnxDpt::kBoolean, KnxValue(true));
    CheckRoundTrip(KnxDpt::kUnsigned8, KnxValue(uint8_t{200}));
    CheckRoundTrip(KnxDpt::kUnsigned16, KnxValue(uint16_t{50000}));
    CheckRoundTrip(KnxDpt::kFloat16, KnxValue(21.5f), 0.02f);
    CheckRoundTrip(KnxDpt::kUnsigned32, KnxValue(uint32_t{4000000000U}));
    CheckRoundTrip(KnxDpt::kSigned32, KnxValue(int32_t{-2000000000}));
    CheckRoundTrip(KnxDpt::kFloat32, KnxValue(123.25f));

    std::vector<uint8_t> encoded;
    assert(KnxEncodeValue(KnxDpt::kBoolean, KnxValue(true), encoded));
    assert(encoded.size() == 1 && encoded[0] == 1);
    assert(KnxEncodeValue(KnxDpt::kUnsigned16, KnxValue(uint16_t{0x1234}), encoded));
    assert(encoded.size() == 3 && encoded[0] == 0 &&
           encoded[1] == 0x12 && encoded[2] == 0x34);
    assert(!KnxEncodeValue(KnxDpt::kUnsigned16, KnxValue(true), encoded));
}

}  // namespace

int main() {
    TestAddresses();
    TestDptSelectionAndValues();
    TestRoundTrips();
    std::cout << "KNX object tests passed\n";
    return 0;
}