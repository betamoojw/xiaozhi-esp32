#include "knx_object.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string_view>

namespace {
enum class Rule { kValue, kEnum, kMask, kUnsupported };
struct Subtype {
    uint16_t main, subtype;
    Rule rule;
    uint16_t argument;
};
constexpr Subtype kSubtypes[] = {
#include "knx_subtypes.inc"
};
constexpr uint32_t kEnums[][8] = {
#include "knx_enums.inc"
};

const Subtype* Find(const KnxDpt& dpt) {
    const auto* end = std::end(kSubtypes);
    const auto* found = std::lower_bound(std::begin(kSubtypes), end, dpt,
        [](const Subtype& a, const KnxDpt& b) {
            return a.main < b.main || (a.main == b.main && a.subtype < b.subtype);
        });
    return found != end && found->main == dpt.main && found->subtype == dpt.subtype
               ? found : nullptr;
}

template <typename T>
bool Is(const KnxValue& value) { return std::holds_alternative<T>(value); }

bool Date(unsigned year, unsigned month, unsigned day) {
    constexpr unsigned days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (month < 1 || month > 12 || day < 1) return false;
    return day <= days[month-1] + (month == 2 && year % 4 == 0 &&
                                  (year % 100 != 0 || year % 400 == 0));
}

bool Utf8(const std::string& text) {
    size_t i = 0;
    while (i < text.size()) {
        auto c = static_cast<uint8_t>(text[i++]);
        if (c < 0x80) continue;
        unsigned count, code, minimum;
        if (c >= 0xc2 && c <= 0xdf) { count=1; code=c&31; minimum=0x80; }
        else if (c >= 0xe0 && c <= 0xef) { count=2; code=c&15; minimum=0x800; }
        else if (c >= 0xf0 && c <= 0xf4) { count=3; code=c&7; minimum=0x10000; }
        else return false;
        if (text.size()-i < count) return false;
        while (count--) {
            c = static_cast<uint8_t>(text[i++]);
            if ((c&0xc0) != 0x80) return false;
            code = (code<<6) | (c&63);
        }
        if (code < minimum || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff)) return false;
    }
    return true;
}

bool Code(const std::string& text, bool region) {
    if (text.size() != 2) return false;
    std::string code = text;
    for (char& c : code) {
        if (c >= 'A' && c <= 'Z') c += 'a'-'A';
        if (c < 'a' || c > 'z') return false;
    }
    // Tables 3 and 4 of the pinned KNX standard, including its legacy codes.
    constexpr std::string_view languages =
        "aa ab ae af ak am an ar as av ay az ba be bg bh bi bm bn bo br bs ca ce ch co cr cs cu cv cy "
        "da de dv dz ee el en eo es et eu fa ff fi fj fo fr fy ga gd gl gn gu gv ha he hi ho hr ht hu "
        "hy hz ia id ie ig ii ik io is it iu ja jv ka kg ki kj kk kl km kn ko kr ks ku kv kw ky la lb "
        "lg li ln lo lt lu lv mg mh mi mk ml mn mo mr ms mt my na nb nd ne ng nl nn no nr nv ny oc oj "
        "om or os pa pi pl ps pt qu rm rn ro ru rw sa sc sd se sg sh si sk sl sm sn so sq sr ss st su "
        "sv sw ta te tg th ti tk tl tn to tr ts tt tw ty ug uk ur uz ve vi vo wa wo xh yi yo za zh zu";
    constexpr std::string_view regions =
#include "knx_regions.inc"
        ;
    const auto codes = region ? regions : languages;
    for (size_t i=0; i+2 <= codes.size(); i+=3) if (codes.substr(i,2)==code) return true;
    return false;
}

bool Value(const KnxDpt& d, const KnxValue& v) {
    const bool sub = d.has_subtype;
    if (Is<std::monostate>(v))
        return sub && (d.main == 9 || (d.main == 8 && d.subtype == 10) ||
                       (d.main == 20 && d.subtype == 1200));
    switch (d.main) {
        case 1: return Is<bool>(v);
        case 2: return Is<knx_dpt2_control_t>(v);
        case 3: {
            auto p=std::get_if<knx_dpt3_control_t>(&v); return p && p->step_code<=7;
        }
        case 4:
            if (sub && d.subtype==1) {
                auto p=std::get_if<std::string>(&v);
                return p && p->size()==1 && static_cast<uint8_t>((*p)[0])<=127;
            }
            return Is<uint8_t>(v);
        case 5:
            if (sub && (d.subtype==1 || d.subtype==3)) {
                auto p=std::get_if<float>(&v);
                return p && std::isfinite(*p) && *p>=0 && *p<=(d.subtype==1 ? 100 : 360);
            }
            return Is<uint8_t>(v) && (!sub || d.subtype!=6 || std::get<uint8_t>(v)<255);
        case 6: {
            auto p=std::get_if<int8_t>(&v);
            if (!p) return false;
            if (!sub || d.subtype!=20) return true;
            const unsigned mode=static_cast<uint8_t>(*p)&7;
            return mode==1 || mode==2 || mode==4;
        }
        case 7: return Is<uint16_t>(v);
        case 8:
            return Is<int16_t>(v) && (!sub || d.subtype!=10 || std::get<int16_t>(v)!=32767);
        case 9: {
            auto p=std::get_if<float>(&v);
            if (!p || !std::isfinite(*p)) return false;
            float minimum=sub ? -671088.64f : KNX_DPT9_ENCODER_MIN_VALUE;
            if (sub && d.subtype==1) minimum=-273.0f;
            if (sub && d.subtype==27) minimum=-459.6f;
            if (sub && ((d.subtype>=4 && d.subtype<=8) || d.subtype>=28)) minimum=0;
            // 0x7fff is invalid, never a numeric sample for a configured subtype.
            return *p>=minimum && (sub ? *p<670433.28f : *p<=KNX_DPT9_MAX_VALUE);
        }
        case 10: {
            auto p=std::get_if<knx_dpt10_time_t>(&v);
            return p && p->weekday<=7 && p->hour<=23 && p->minute<=59 && p->second<=59;
        }
        case 11: {
            auto p=std::get_if<knx_dpt11_date_t>(&v);
            return p && p->year<=99 && Date((p->year>=90 ? 1900 : 2000)+p->year,p->month,p->day);
        }
        case 12: return Is<uint32_t>(v);
        case 13: return Is<int32_t>(v);
        case 14: return Is<float>(v) && std::isfinite(std::get<float>(v));
        case 15: {
            auto p=std::get_if<uint32_t>(&v);
            if (!p) return false;
            if (sub) for (unsigned shift=8;shift<32;shift+=4) if (((*p>>shift)&15)>9) return false;
            return true;
        }
        case 16: case 24: case 28: case 234: {
            auto p=std::get_if<std::string>(&v);
            if (!p || p->find('\0')!=std::string::npos) return false;
            if (d.main==234) return sub ? Code(*p,d.subtype==2) :
                p->size()==2 && (*p)[0]>='a' && (*p)[0]<='z' && (*p)[1]>='a' && (*p)[1]<='z';
            if (p->size()>(d.main==16 ? 14U : 253U)) return false;
            if (d.main==16 && sub && d.subtype==0)
                return std::all_of(p->begin(),p->end(),[](unsigned char c){return c<=127;});
            return d.main!=28 || Utf8(*p);
        }
        case 17: return Is<uint8_t>(v) && std::get<uint8_t>(v)<=63;
        case 18: {
            auto p=std::get_if<knx_dpt18_scene_control_t>(&v); return p && p->scene_number<=63;
        }
        case 19: {
            auto p=std::get_if<KnxDateTime>(&v);
            if (!p || p->month>15 || p->day>31 || p->weekday>7 || p->hour>31 ||
                p->minute>63 || p->second>63) return false;
            // With no year, February 29 is meaningful (e.g. a recurring schedule).
            if (p->date_valid && !Date(p->year_valid ? 1900+p->year : 2000,p->month,p->day)) return false;
            return !p->time_valid || (p->hour<=24 && p->minute<=59 && p->second<=59 &&
                (p->hour!=24 || (p->minute==0 && p->second==0)));
        }
        case 20: case 21: case 23: case 25: case 30: case 31: {
            if (d.main==30 && sub) return Is<uint32_t>(v) && std::get<uint32_t>(v)<=0xffffff;
            if (d.main==31 && !sub) return Is<uint32_t>(v) && std::get<uint32_t>(v)<=0xffffff;
            auto p=std::get_if<uint8_t>(&v);
            if (!p) return false;
            if (d.main==23) return *p<=((sub && d.subtype==2) ? 2 : 3);
            if (d.main==25 && sub) return (*p&0xcc)==0;
            if (d.main==31) return *p<=7;
            if (!sub) return true;
            const auto* entry=Find(d);
            if (entry->rule==Rule::kEnum) return (kEnums[entry->argument][*p/32] & (1U<<(*p%32)))!=0;
            if (entry->rule==Rule::kMask)
                return (*p & ~entry->argument)==0 && (d.subtype!=1000 || (*p&1));
            return true;
        }
        case 22: {
            auto p=std::get_if<uint16_t>(&v);
            return p && (!sub || (*p & ~Find(d)->argument)==0);
        }
        case 26: {
            auto p=std::get_if<knx_dpt26_scene_info_t>(&v); return p && p->scene_number<=63;
        }
        case 27: return Is<knx_dpt27_combined_status_t>(v);
        case 29: return Is<int64_t>(v);
        case 232: return Is<knx_dpt232_color_t>(v);
        case 251: {
            auto p=std::get_if<knx_dpt251_color_t>(&v); return p && (p->valid_channels&0xf0)==0;
        }
        default: return false;
    }
}
}  // namespace

bool KnxValidateDpt(const KnxDpt& dpt) {
    if (!((dpt.main>=1 && dpt.main<=31) || dpt.main==232 || dpt.main==234 || dpt.main==251)) return false;
    if (!dpt.has_subtype) return dpt.subtype==0;
    const auto* entry=Find(dpt);
    return entry && entry->rule!=Rule::kUnsupported;
}

bool KnxValidateValue(const KnxDpt& dpt, const KnxValue& value, std::string* error) {
    const bool known=KnxValidateDpt(dpt);
    const bool valid=known && Value(dpt,value);
    if (error) *error=valid ? "" : KnxDptName(dpt) + (known ?
        ": invalid representation, range, encoding, enum or reserved bits" : ": unknown or unsupported subtype");
    return valid;
}
