#include "knx_config.h"

#include <cJSON.h>

#include <algorithm>
#include <cstring>
#include <iterator>
#include <set>
#include <utility>

namespace {

constexpr size_t kMaximumIdLength = 48;
constexpr size_t kMaximumNameLength = 80;
constexpr size_t kMaximumDescriptionLength = 192;

bool IsKnownField(const char* name) {
    constexpr const char* kFields[] = {
        "id", "name", "description", "group_address", "datapoint_type",
        "readable", "writable",
    };
    return name != nullptr && std::any_of(std::begin(kFields), std::end(kFields),
        [name](const char* field) { return std::strcmp(name, field) == 0; });
}

bool JsonString(const cJSON* object, const char* name, std::string& value,
                bool required, size_t maximum_length) {
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(object, name);
    if (item == nullptr && !required) {
        value.clear();
        return true;
    }
    if (!cJSON_IsString(item) || item->valuestring == nullptr) {
        return false;
    }
    value = item->valuestring;
    return (!required || !value.empty()) && value.size() <= maximum_length;
}

bool JsonBoolean(const cJSON* object, const char* name, bool& value) {
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(object, name);
    if (!cJSON_IsBool(item)) {
        return false;
    }
    value = cJSON_IsTrue(item);
    return true;
}

bool ValidateObject(const KnxCommunicationObject& object,
                    const std::vector<KnxCommunicationObject>& objects,
                    std::string& error) {
    if (!object.readable && !object.writable) {
        error = "KNX object must be readable, writable, or both";
        return false;
    }
    if (std::any_of(objects.begin(), objects.end(), [&object](const auto& existing) {
            return existing.id == object.id;
        })) {
        error = "Duplicate KNX object ID: " + object.id;
        return false;
    }
    if (std::any_of(objects.begin(), objects.end(), [&object](const auto& existing) {
            return existing.parsed_group_address == object.parsed_group_address;
        })) {
        error = "Duplicate KNX group address: " + object.group_address;
        return false;
    }
    return true;
}

}  // namespace

bool KnxParseConfiguration(const std::string& json_text, size_t maximum_objects,
                           size_t maximum_group_addresses,
                           std::vector<KnxCommunicationObject>& objects,
                           std::string& canonical_json, std::string& error) {
    objects.clear();
    canonical_json.clear();
    error.clear();
    if (json_text.empty() || json_text.size() > kKnxMaximumConfigurationLength) {
        error = "KNX configuration is empty or exceeds the NVS string limit";
        return false;
    }

    cJSON* root = cJSON_ParseWithLengthOpts(json_text.c_str(), json_text.size() + 1,
                                            nullptr, true);
    if (!cJSON_IsArray(root)) {
        cJSON_Delete(root);
        error = "KNX object configuration must be a JSON array";
        return false;
    }

    bool valid = true;
    const cJSON* item = nullptr;
    cJSON_ArrayForEach(item, root) {
        if (objects.size() >= maximum_objects || !cJSON_IsObject(item)) {
            error = "KNX object configuration exceeds limits or contains a non-object";
            valid = false;
            break;
        }
        std::set<std::string> fields;
        for (const cJSON* field = item->child; field != nullptr; field = field->next) {
            if (!IsKnownField(field->string)) {
                error = std::string("Unknown KNX object field: ") +
                        (field->string == nullptr ? "<unnamed>" : field->string);
                valid = false;
                break;
            }
            if (!fields.insert(field->string).second) {
                error = std::string("Duplicate KNX object field: ") + field->string;
                valid = false;
                break;
            }
        }
        if (!valid) {
            break;
        }

        KnxCommunicationObject object;
        std::string datapoint_type;
        if (!JsonString(item, "id", object.id, true, kMaximumIdLength) ||
            !JsonString(item, "name", object.name, true, kMaximumNameLength) ||
            !JsonString(item, "description", object.description, false,
                        kMaximumDescriptionLength) ||
            !JsonString(item, "group_address", object.group_address, true, 10) ||
            !JsonString(item, "datapoint_type", datapoint_type, true, 16) ||
            !JsonBoolean(item, "readable", object.readable) ||
            !JsonBoolean(item, "writable", object.writable) ||
            !KnxParseGroupAddress(object.group_address, object.parsed_group_address) ||
            !KnxParseDpt(datapoint_type, object.datapoint_type)) {
            error = "Invalid KNX communication object configuration";
            valid = false;
            break;
        }
        object.group_address = KnxFormatGroupAddress(object.parsed_group_address);
        if (!ValidateObject(object, objects, error)) {
            valid = false;
            break;
        }
        objects.push_back(std::move(object));
    }

    if (valid && objects.size() > maximum_group_addresses) {
        error = "KNX configuration exceeds component callback capacity";
        valid = false;
    }
    if (valid) {
        char* printed = cJSON_PrintUnformatted(root);
        if (printed == nullptr) {
            error = "Could not serialize KNX configuration";
            valid = false;
        } else {
            canonical_json = printed;
            cJSON_free(printed);
            if (canonical_json.size() > kKnxMaximumConfigurationLength) {
                error = "Canonical KNX configuration exceeds the NVS string limit";
                valid = false;
            }
        }
    }
    cJSON_Delete(root);
    if (!valid) {
        objects.clear();
        canonical_json.clear();
    }
    return valid;
}