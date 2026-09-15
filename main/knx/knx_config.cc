#include "knx_config.h"

#ifndef XIAOZHI_KNX_CONFIG_PARSER_ONLY
#include "settings.h"

#include <esp_err.h>
#include <esp_littlefs.h>
#include <unistd.h>
#endif
#include <cJSON.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <set>
#include <utility>

namespace {

constexpr size_t kMaximumIdLength = 48;
constexpr size_t kMaximumNameLength = 80;
constexpr size_t kMaximumDescriptionLength = 192;
#ifndef XIAOZHI_KNX_CONFIG_PARSER_ONLY
constexpr char kLittleFsPartitionLabel[] = "littlefs";
#endif

bool IsKnownField(const char* name) {
    constexpr const char* kFields[] = {
        "id", "name", "description", "group_address", "datapoint_type", "readable", "writable",
        "unit",
    };
    return name != nullptr &&
           std::any_of(std::begin(kFields), std::end(kFields),
                       [name](const char* field) { return std::strcmp(name, field) == 0; });
}

std::string ObjectFieldError(size_t index, const char* field) {
    return "Invalid KNX communication object " + std::to_string(index) + " field '" + field +
           "'";
}

bool JsonString(const cJSON* object, const char* name, std::string& value, bool required,
                size_t maximum_length) {
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(object, name);
    if (item == nullptr && !required) {
        value.clear();
        return true;
    }
    if (!required && cJSON_IsNull(item)) {
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
                    const std::vector<KnxCommunicationObject>& objects, size_t index,
                    std::string& error) {
    if (!object.readable && !object.writable) {
        error = ObjectFieldError(index, "readable/writable") + ": at least one must be true";
        return false;
    }
    if (std::any_of(objects.begin(), objects.end(),
                    [&object](const auto& existing) { return existing.id == object.id; })) {
        error = ObjectFieldError(index, "id") + ": duplicate ID '" + object.id + "'";
        return false;
    }
    if (std::any_of(objects.begin(), objects.end(), [&object](const auto& existing) {
            return existing.parsed_group_address == object.parsed_group_address;
        })) {
        error = ObjectFieldError(index, "group_address") + ": duplicate address '" +
            object.group_address + "'";
        return false;
    }
    return true;
}

}  // namespace

#ifndef XIAOZHI_KNX_CONFIG_PARSER_ONLY
bool KnxLoadRuntimeConfiguration(std::string& json_text, bool& found, std::string& error) {
    json_text.clear();
    found = false;
    error.clear();
    if (!esp_littlefs_mounted(kLittleFsPartitionLabel)) {
        error = "LittleFS is not mounted";
        return false;
    }

    errno = 0;
    std::ifstream input(kKnxRuntimeConfigurationPath, std::ios::binary | std::ios::ate);
    if (!input) {
        if (errno == ENOENT) {
            return true;
        }
        error = std::string("Could not open KNX runtime configuration: ") + std::strerror(errno);
        return false;
    }
    const std::streamsize size = input.tellg();
    if (size <= 0 || static_cast<size_t>(size) > kKnxMaximumConfigurationLength) {
        error = "KNX runtime configuration is empty or exceeds the size limit";
        return false;
    }
    json_text.resize(static_cast<size_t>(size));
    input.seekg(0);
    if (!input.read(json_text.data(), size)) {
        error = "Could not read the complete KNX runtime configuration";
        json_text.clear();
        return false;
    }
    found = true;
    return true;
}

bool KnxLoadLegacyConfiguration(std::string& json_text, bool& found, std::string& error) {
    json_text.clear();
    found = false;
    error.clear();
    Settings settings(kKnxSettingsNamespace);
    const esp_err_t result = settings.GetString(kKnxSettingsKey, json_text);
    if (result == ESP_ERR_NVS_NOT_FOUND) {
        return true;
    }
    if (result != ESP_OK) {
        error = std::string("Could not load legacy KNX configuration from NVS: ") +
                esp_err_to_name(result);
        return false;
    }
    found = true;
    return true;
}

bool KnxLoadRuntimeConfigurationUpload(std::string& json_text, std::string& error) {
    json_text.clear();
    error.clear();
    if (!esp_littlefs_mounted(kLittleFsPartitionLabel)) {
        error = "LittleFS is not mounted";
        return false;
    }

    std::ifstream input(kKnxRuntimeConfigurationUploadPath, std::ios::binary | std::ios::ate);
    if (!input) {
        error = std::string("Could not open KNX configuration upload: ") + std::strerror(errno);
        return false;
    }
    const std::streamsize size = input.tellg();
    if (size <= 0 || static_cast<size_t>(size) > kKnxMaximumConfigurationLength) {
        error = "KNX configuration upload is empty or exceeds the size limit";
        return false;
    }
    json_text.resize(static_cast<size_t>(size));
    input.seekg(0);
    if (!input.read(json_text.data(), size)) {
        error = "Could not read the complete KNX configuration upload";
        json_text.clear();
        return false;
    }
    return true;
}

bool KnxWriteRuntimeConfiguration(const std::string& json_text, std::string& error) {
    error.clear();
    if (!esp_littlefs_mounted(kLittleFsPartitionLabel)) {
        error = "LittleFS is not mounted";
        return false;
    }
    if (json_text.empty() || json_text.size() > kKnxMaximumConfigurationLength) {
        error = "KNX configuration is empty or exceeds the size limit";
        return false;
    }

    const std::string write_path = kKnxRuntimeConfigurationUploadPath;
    FILE* file = std::fopen(write_path.c_str(), "wb");
    if (file == nullptr) {
        error = std::string("Could not create KNX configuration file: ") + std::strerror(errno);
        return false;
    }

    bool success = std::fwrite(json_text.data(), 1, json_text.size(), file) == json_text.size();
    if (success) {
        success = std::fflush(file) == 0 && fsync(fileno(file)) == 0;
    }
    if (std::fclose(file) != 0) {
        success = false;
    }
    if (!success) {
        error = std::string("Could not write KNX configuration file: ") + std::strerror(errno);
        std::remove(write_path.c_str());
        return false;
    }
    if (std::rename(write_path.c_str(), kKnxRuntimeConfigurationPath) != 0) {
        error = std::string("Could not publish KNX configuration file: ") + std::strerror(errno);
        std::remove(write_path.c_str());
        return false;
    }
    return true;
}
#endif

bool KnxParseConfiguration(const std::string& json_text, size_t maximum_objects,
                           size_t maximum_group_addresses,
                           std::vector<KnxCommunicationObject>& objects,
                           std::string& canonical_json, std::string& error,
                           std::string* physical_address) {
    objects.clear();
    canonical_json.clear();
    error.clear();
    if (physical_address != nullptr) {
        physical_address->clear();
    }
    if (json_text.empty() || json_text.size() > kKnxMaximumConfigurationLength) {
        error = "KNX configuration is empty or exceeds the size limit";
        return false;
    }

    cJSON* root = cJSON_ParseWithLengthOpts(json_text.c_str(), json_text.size() + 1, nullptr, true);
    if (root == nullptr) {
        error = "Invalid JSON in KNX configuration";
        return false;
    }

    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        error = "KNX configuration root must be a JSON object";
        return false;
    }
    std::string media_type;
    if (!JsonString(root, "media_type", media_type, true, 16) || media_type != "knx_ip") {
        cJSON_Delete(root);
        error = "Invalid KNX root field 'media_type': expected 'knx_ip'";
        return false;
    }
    const cJSON* media_parameters = cJSON_GetObjectItemCaseSensitive(root, "media_parameters");
    if (!cJSON_IsObject(media_parameters)) {
        cJSON_Delete(root);
        error = "Invalid KNX root field 'media_parameters': expected an object";
        return false;
    }
    std::string multicast_address;
    std::string transport_mode;
    std::string interface_identifier;
    const cJSON* udp_port = cJSON_GetObjectItemCaseSensitive(media_parameters, "udp_port");
    if (!JsonString(media_parameters, "multicast_address", multicast_address, true, 45)) {
        cJSON_Delete(root);
        error = "Invalid KNX root field 'media_parameters.multicast_address'";
        return false;
    }
    if (!cJSON_IsNumber(udp_port) || udp_port->valuedouble < 1 || udp_port->valuedouble > 65535 ||
        udp_port->valuedouble != udp_port->valueint) {
        cJSON_Delete(root);
        error = "Invalid KNX root field 'media_parameters.udp_port'";
        return false;
    }
    if (!JsonString(media_parameters, "transport_mode", transport_mode, true, 16) ||
        transport_mode != "routing") {
        cJSON_Delete(root);
        error = "Invalid KNX root field 'media_parameters.transport_mode': expected 'routing'";
        return false;
    }
    if (!JsonString(media_parameters, "interface_identifier", interface_identifier, true, 80)) {
        cJSON_Delete(root);
        error = "Invalid KNX root field 'media_parameters.interface_identifier'";
        return false;
    }
    bool nat = false;
    if (!JsonBoolean(media_parameters, "nat", nat)) {
        cJSON_Delete(root);
        error = "Invalid KNX root field 'media_parameters.nat'";
        return false;
    }
    std::string parsed_physical_address;
    knx_address_t parsed_physical_address_value = 0;
    if (!JsonString(root, "physical_address", parsed_physical_address, true, 10) ||
        !KnxParsePhysicalAddress(parsed_physical_address, parsed_physical_address_value)) {
        cJSON_Delete(root);
        error = "Invalid KNX root field 'physical_address'";
        return false;
    }
    if (physical_address != nullptr) {
        *physical_address = KnxFormatPhysicalAddress(parsed_physical_address_value);
    }
    cJSON* objects_array = cJSON_GetObjectItemCaseSensitive(root, "communication_objects");
    if (!cJSON_IsArray(objects_array)) {
        cJSON_Delete(root);
        error = "Invalid KNX root field 'communication_objects': expected an array";
        return false;
    }

    bool valid = true;
    const cJSON* item = nullptr;
    size_t index = 0;
    cJSON_ArrayForEach (item, objects_array) {
        if (objects.size() >= maximum_objects || !cJSON_IsObject(item)) {
            error = ObjectFieldError(index, "object") +
                    ": exceeds the configured limit or is not an object";
            valid = false;
            break;
        }
        std::set<std::string> fields;
        for (const cJSON* field = item->child; field != nullptr; field = field->next) {
            if (!IsKnownField(field->string)) {
                error = ObjectFieldError(index,
                                         field->string == nullptr ? "<unnamed>" : field->string) +
                        ": unknown field";
                valid = false;
                break;
            }
            if (!fields.insert(field->string).second) {
                error = ObjectFieldError(index, field->string) + ": duplicate field";
                valid = false;
                break;
            }
        }
        if (!valid) {
            break;
        }

        KnxCommunicationObject object;
        std::string datapoint_type;
        if (!JsonString(item, "id", object.id, true, kMaximumIdLength)) {
            error = ObjectFieldError(index, "id");
        } else if (!JsonString(item, "name", object.name, true, kMaximumNameLength)) {
            error = ObjectFieldError(index, "name");
        } else if (!JsonString(item, "description", object.description, false,
                               kMaximumDescriptionLength)) {
            error = ObjectFieldError(index, "description");
        } else if (!JsonString(item, "group_address", object.group_address, true, 10) ||
                   !KnxParseGroupAddress(object.group_address, object.parsed_group_address)) {
            error = ObjectFieldError(index, "group_address");
        } else if (!JsonString(item, "datapoint_type", datapoint_type, true, 16) ||
                   !KnxParseDpt(datapoint_type, object.datapoint_type) ||
                   KnxDptName(object.datapoint_type) != datapoint_type) {
            error = ObjectFieldError(index, "datapoint_type");
        } else if (!JsonBoolean(item, "readable", object.readable)) {
            error = ObjectFieldError(index, "readable");
        } else if (!JsonBoolean(item, "writable", object.writable)) {
            error = ObjectFieldError(index, "writable");
        } else if (!JsonString(item, "unit", object.unit, false, 32)) {
            error = ObjectFieldError(index, "unit");
        } else {
            object.group_address = KnxFormatGroupAddress(object.parsed_group_address);
            valid = ValidateObject(object, objects, index, error);
        }
        if (!error.empty()) {
            valid = false;
            break;
        }
        objects.push_back(std::move(object));
        ++index;
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
                error = "Canonical KNX configuration exceeds the size limit";
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