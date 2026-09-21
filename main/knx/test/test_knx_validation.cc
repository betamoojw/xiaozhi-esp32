#include "knx_object.h"

#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <set>
#include <sstream>

namespace {
// Independent transcription of section 2 of KNX 03/07/02 v02.02.01.
const char* const kIds[] = {
    "1:1-19,21-24,100,1200-1201", "2:1-12", "3:7-8", "4:1-2", "5:1,3-6,10",
    "6:1,10,20", "7:1-7,11-13,600", "8:1-7,10-12", "9:1-11,20-30",
    "10:1", "11:1", "12:1,100-102,1200-1201", "13:1-2,10-16,100,1200-1201",
    "14:0-80,1200-1201", "15:0", "16:0-1", "17:1", "18:1", "19:1",
    "20:1-8,11-14,17,20-22,100-115,120-122,600-613,801-804,1001-1005,1200,1202-1209",
    "21:1-2,100-106,601,1000-1002,1010,1200-1201", "22:100-101,1000,1010",
    "23:1-3,102", "24:1", "25:1000", "26:1", "27:1", "28:1", "29:10-12",
    "30:1010", "31:101", "232:600", "234:1-2", "251:600"
};

std::set<unsigned> Numbers(const std::string& ranges) {
    std::set<unsigned> result;
    std::istringstream input(ranges);
    std::string part;
    while (std::getline(input,part,',')) {
        const auto dash=part.find('-');
        const unsigned first=std::stoul(part.substr(0,dash));
        const unsigned last=dash==std::string::npos ? first : std::stoul(part.substr(dash+1));
        for (unsigned n=first;n<=last;++n) result.insert(n);
    }
    return result;
}

std::string Sample(const KnxDpt& d) {
    switch (d.main) {
        case 1: return "on";
        case 2: return "true,false";
        case 3: return "true,7";
        case 4: return d.subtype==1 ? "A" : "65";
        case 6: return d.subtype==20 ? "1" : "0";
        case 10: return "7,23,59,59";
        case 11: return "29,2,24";
        case 16: case 24: case 28: return "KNX";
        case 18: case 26: return "true,63";
        case 19: return "124,2,29,4,24,0,0,false,true,true,true,true,true,false,true,true";
        case 20: return "1";
        case 21: return d.subtype==1000 ? "1" : "0";
        case 27: return "1,65535";
        case 232: return "0,128,255";
        case 234: return d.subtype==1 ? "en" : "DE";
        case 251: return "0,128,255,64,15";
        default: return "0";
    }
}

void Catalog() {
    unsigned count=0;
    for (auto row:kIds) {
        const std::string text=row;
        const auto colon=text.find(':');
        const uint16_t main=static_cast<uint16_t>(std::stoul(text.substr(0,colon)));
        const auto allowed=Numbers(text.substr(colon+1));
        for (uint16_t sub=0;sub<=1300;++sub) {
            KnxDpt d{main,sub,true}, parsed;
            const bool expected=allowed.count(sub)!=0;
            assert(KnxValidateDpt(d)==expected);
            assert(KnxParseDpt(KnxDptName(d),parsed)==expected);
            if (!expected) continue;
            ++count;
            KnxValue v,decoded;
            const auto sample=Sample(d);
            if (!KnxParseValue(d,sample,v)) { std::cerr<<KnxDptName(d)<<" sample "<<sample<<"\n"; std::abort(); }
            std::vector<uint8_t> data;
            assert(KnxEncodeValue(d,v,data));
            assert(KnxDecodeValue(d,data.data(),data.size(),decoded));
            assert(KnxValueToString(v)==KnxValueToString(decoded));
            data.push_back(0);
            assert(!KnxDecodeValue(d,data.data(),data.size(),decoded));
            data.pop_back();
            assert(!KnxDecodeValue(d,data.data(),data.size()-1,decoded));
            assert(!KnxEncodeValue(d,KnxValue(uint64_t{0}==1),data) || main==1);
        }
    }
    assert(count==308); // Two externally defined system subtypes are explicitly unsupported.
}

void Enums() {
    // Encoding entries take precedence over contradictory summary ranges:
    // e.g. 20.001 reserves 3; 20.007 reserves 0; 20.105 has holes at 18 and 19.
    struct Case { uint16_t sub; const char* values; };
    const Case cases[] = {
        {1,"0-2"},{2,"0-2"},{3,"0-2"},{4,"0-3"},{5,"0-2,17-255"},{6,"0-1,10-14,20,30,40,50"},
        {7,"1-3"},{8,"0-2"},{11,"0-18"},{12,"0-4"},{13,"0-25"},{14,"0-12"},{17,"0-4"},
        {20,"1-2"},{21,"0-9"},{22,"0-2"},{100,"0-3"},{101,"1-3"},{102,"0-4"},{103,"0-4"},
        {104,"0-2"},{105,"0-17,20"},{106,"0-5"},{107,"0-2"},{108,"1-5"},{109,"1-4"},
        {110,"1-3"},{111,"0-2"},{112,"0-2"},{113,"0-2"},{114,"0-8,10-13,32-33,40-41,255"},
        {115,"0-2"},{120,"1-2"},{121,"0-1"},{122,"0-2"},{600,"0-6"},{601,"0-4"},
        {602,"0-15"},{603,"0-2"},{604,"0-1"},{605,"1-2"},{606,"0-3"},{607,"1-4"},
        {608,"0-2"},{609,"0-8"},{610,"0-8"},{611,"1-6"},{612,"0-4"},{613,"1-8"},
        {801,"0-4"},{802,"0-6"},{803,"1-4"},{804,"0-1"},{1001,"1-7"},{1002,"0-2"},
        {1003,"0-3"},{1004,"0-2,5"},{1005,"1-14,16-55"},{1200,"0-2"},{1202,"0-3"},
        {1203,"0-6"},{1204,"0-2"},{1205,"0-2"},{1206,"0-3"},{1207,"0-3"},{1208,"0-1"},{1209,"0-4"}
    };
    for (const auto& c:cases) {
        const auto allowed=Numbers(c.values);
        for (unsigned n=0;n<256;++n) {
            const KnxDpt d{20,c.sub,true};
            KnxValue v=std::string("unchanged");
            const uint8_t data[]={0,static_cast<uint8_t>(n)};
            const bool expected=allowed.count(n)!=0;
            assert(KnxParseValue(d,std::to_string(n),v)==expected);
            assert(KnxDecodeValue(d,data,2,v)==expected);
            std::vector<uint8_t> encoded{99};
            assert(KnxEncodeValue(d,KnxValue(static_cast<uint8_t>(n)),encoded)==expected);
            if (!expected) assert(encoded.empty());
        }
    }
}

void Boundaries() {
    struct Case { KnxDpt d; const char* good; const char* bad; };
    const Case cases[]={
        {{5,1,true},"100","100.01"},{{5,1,true},"0","-0.01"},
        {{5,3,true},"360","360.1"},{{5,6,true},"254","255"},
        {{5,4,true},"255","256"},{{6,20,true},"-127","3"},
        {{7,600,true},"65535","65536"},{{8,10,true},"32766","32767"},
        {{9,1,true},"-273","-273.01"},{{9,27,true},"-459.6","-459.61"},
        {{9,7,true},"101","-0.01"},{{9,26,true},"-1","670433.28"},
        {{9,2,true},"-671088.64","-671089"},{{9,30,true},"1","-1"},
        {{10,1,true},"0,23,59,59","0,24,0,0"},
        {{11,1,true},"29,2,0","29,2,1"},{{11,1,true},"31,12,99","31,4,99"},
        {{15,0,true},"305419896","2882400000"},
        {{16,0,true},"12345678901234","123456789012345"},
        {{17,1,true},"63","64"},{{18,1,true},"true,63","true,64"},
        {{23,2,true},"2","3"},{{25,1000,true},"51","52"},
        {{30,1010,true},"16777215","16777216"},{{31,101,true},"7","8"},
        {{234,1,true},"EN","zz"},{{234,2,true},"zz","XX"},
        {{251,600,true},"0,255,1,2,15","0,255,1,2,16"}
    };
    for (const auto& c:cases) {
        KnxValue v;
        if (!KnxParseValue(c.d,c.good,v)) { std::cerr<<KnxDptName(c.d)<<" boundary "<<c.good<<"\n"; std::abort(); }
        const auto before=KnxValueToString(v);
        assert(!KnxParseValue(c.d,c.bad,v));
        assert(KnxValueToString(v)==before);
    }
    KnxValue v=true;
    for (const char* text:{"", " 1", "1 ", "0x1p0", "nan", "inf", "1e1000", "1e-9999", ".", "1e", "1junk"})
        assert(!KnxParseValue({14,0,true},text,v));
    for (float f:{std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}) {
        std::vector<uint8_t> out{1};
        assert(!KnxEncodeValue({14,0,true},KnxValue(f),out) && out.empty());
    }
    for (const char* text:{"DPT-1.000","DPT-1.999","DPT-20.1000","DPT-7.010","1.","1.1x","1.65536","1.-1","1.000001"}) {
        KnxDpt d{1,1,true}; assert(!KnxParseDpt(text,d)); assert((d==KnxDpt{1,1,true}));
    }
    assert(!KnxValidateDpt({1,1,false}));
    assert(!KnxParseValue({1,999,true},"on",v));
    assert(!KnxDecodeValue({1},nullptr,1,v));
    assert(!KnxParseValue({16,0,true},std::string(1,'\x80'),v));
    assert(KnxParseValue({16,1,true},std::string(1,'\xff'),v));
    assert(!KnxParseValue({16,1,true},std::string("a\0b",3),v));
    assert(!KnxParseValue({28,1,true},std::string("\xc0\x80",2),v));
    assert(!KnxParseValue({28,1,true},std::string("\xed\xa0\x80",3),v));
    assert(!KnxParseValue({28,1,true},std::string("\xf4\x90\x80\x80",4),v));
    assert(KnxParseValue({28,1,true},std::string("\xf0\x9f\x98\x80",4),v));
    assert(KnxParseValue({24,1,true},std::string(253,'x'),v));
    assert(!KnxParseValue({24,1,true},std::string(254,'x'),v));
}

void Vectors() {
    struct Case { KnxDpt d; const char* text; std::vector<uint8_t> bytes; };
    const Case cases[]={
        {{5,1,true},"100",{0,255}},{{5,3,true},"180",{0,128}},
        {{9,1,true},"20",{0,0x07,0xd0}},{{9,2,true},"-671088.64",{0,0xf8,0}},
        {{23,1,true},"3",{3}},{{30,1010,true},"1193046",{0,0x12,0x34,0x56}},
        {{31,101,true},"7",{7}},{{26,1,true},"true,1",{0,1}},
        {{27,1,true},"1,32768",{0,0x80,0,0,1}},
        {{234,1,true},"EN",{0,'e','n'}},{{234,2,true},"de",{0,'D','E'}}
    };
    for (const auto& c:cases) {
        KnxValue v; std::vector<uint8_t> data;
        assert(KnxParseValue(c.d,c.text,v)); assert(KnxEncodeValue(c.d,v,data));
        assert(data==c.bytes); assert(KnxDecodeValue(c.d,data.data(),data.size(),v));
    }
    for (auto d:{KnxDpt{9,1,true},KnxDpt{8,10,true},KnxDpt{20,1200,true}}) {
        KnxValue v; std::vector<uint8_t> data;
        assert(KnxParseValue(d,"invalid",v)); assert(KnxEncodeValue(d,v,data));
        assert(data.back()==255); v=true;
        assert(!KnxDecodeValue(d,data.data(),data.size(),v) && std::get<bool>(v));
    }
    KnxValue v;
    const KnxDpt date{19,1,true};
    const uint8_t end_day[]={0,124,2,29,24,0,0,0,0};
    assert(KnxDecodeValue(date,end_day,sizeof(end_day),v));
    assert(std::get<KnxDateTime>(v).hour==24);
    auto p=std::get<KnxDateTime>(v); p.second=1;
    assert(!KnxValidateValue(date,p));
    p.second=0; p.year=123; p.year_valid=false;
    assert(KnxValidateValue(date,p)); // February 29 with unspecified year.
    p.year_valid=true; assert(!KnxValidateValue(date,p));
    const uint8_t bad_text[]={0,'a',0,'b',0};
    assert(!KnxDecodeValue({24,1,true},bad_text,sizeof(bad_text),v));
}
}

void TestSubtypeValidation() {
    Catalog(); Enums(); Boundaries(); Vectors();
}
