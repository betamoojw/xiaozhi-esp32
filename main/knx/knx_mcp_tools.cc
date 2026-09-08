#include "knx_mcp_tools.h"

#include "knx_manager.h"
#include "mcp_server.h"

#include <cJSON.h>

#include <algorithm>
#include <stdexcept>

namespace {

void AddValue(cJSON* json, const KnxValue& value) {
    std::visit([json](const auto& item) {
        using Type = std::decay_t<decltype(item)>;
        if constexpr (std::is_same_v<Type, bool>) {
            cJSON_AddBoolToObject(json, "value", item);
        } else {
            cJSON_AddNumberToObject(json, "value", item);
        }
    }, value);
}

cJSON* ObjectToJson(const KnxCommunicationObject& object, bool include_description) {
    cJSON* json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "id", object.id.c_str());
    cJSON_AddStringToObject(json, "name", object.name.c_str());
    if (include_description && !object.description.empty()) {
        cJSON_AddStringToObject(json, "description", object.description.c_str());
    }
    cJSON_AddStringToObject(json, "group_address", object.group_address.c_str());
    cJSON_AddStringToObject(json, "datapoint_type", KnxDptName(object.datapoint_type));
    cJSON_AddBoolToObject(json, "readable", object.readable);
    cJSON_AddBoolToObject(json, "writable", object.writable);
    cJSON_AddBoolToObject(json, "valid", object.valid);
    if (object.valid) {
        AddValue(json, object.current_value);
        cJSON_AddNumberToObject(json, "last_update_ms",
                                static_cast<double>(object.last_update_ms));
    }
    return json;
}

void ThrowOnFailure(bool success, const std::string& error) {
    if (!success) {
        throw std::runtime_error(error);
    }
}

}  // namespace

void RegisterKnxMcpTools(McpServer& server) {
    auto& manager = KnxManager::GetInstance();

    server.AddTool("self.knx.get_status",
        "Gets KNXnet/IP routing availability and cache health. Use this before KNX operations when availability is unknown. A running state means multicast routing is active, not that a KNX router has been authenticated or proven reachable.",
        PropertyList(),
        [&manager](const PropertyList&) -> ReturnValue {
            cJSON* json = cJSON_CreateObject();
            cJSON_AddStringToObject(json, "state", manager.GetStateName());
            cJSON_AddStringToObject(json, "mode", "routing");
            cJSON_AddStringToObject(json, "endpoint", manager.GetEndpoint().c_str());
            cJSON_AddStringToObject(json, "physical_address",
                                    manager.GetPhysicalAddress().c_str());
            cJSON_AddNumberToObject(json, "object_count", manager.GetObjectCount());
            cJSON_AddNumberToObject(json, "valid_object_count",
                                    manager.GetValidObjectCount());
            cJSON_AddNumberToObject(json, "last_communication_ms",
                                    static_cast<double>(manager.GetLastCommunicationMs()));
            const std::string error = manager.GetLastError();
            if (!error.empty()) cJSON_AddStringToObject(json, "last_error", error.c_str());
            return json;
        });

    server.AddTool("self.knx.list_objects",
        "Lists configured KNX communication objects so semantic names can be mapped to object IDs. Use this when the requested KNX device or object ID is unknown. Results are paginated; continue with next_offset when has_more is true.",
        PropertyList({
            Property("offset", kPropertyTypeInteger, 0, 0, CONFIG_XIAOZHI_KNX_IP_MAX_OBJECTS),
            Property("limit", kPropertyTypeInteger, 10, 1, 20),
        }),
        [&manager](const PropertyList& properties) -> ReturnValue {
            const size_t offset = properties["offset"].value<int>();
            const size_t limit = properties["limit"].value<int>();
            const auto objects = manager.GetObjects(offset, limit);
            const size_t total = manager.GetObjectCount();
            cJSON* json = cJSON_CreateObject();
            cJSON* array = cJSON_AddArrayToObject(json, "objects");
            for (const auto& object : objects) {
                cJSON_AddItemToArray(array, ObjectToJson(object, false));
            }
            const size_t next_offset = offset + objects.size();
            cJSON_AddNumberToObject(json, "total", total);
            cJSON_AddBoolToObject(json, "has_more", next_offset < total);
            if (next_offset < total) cJSON_AddNumberToObject(json, "next_offset", next_offset);
            return json;
        });

    server.AddTool("self.knx.get_object",
        "Gets the latest cached value and metadata for one configured KNX object. Use this for status questions such as whether a light is on or the current temperature. If valid is false, report the value as unknown and optionally request a read.",
        PropertyList({Property("object_id", kPropertyTypeString)}),
        [&manager](const PropertyList& properties) -> ReturnValue {
            KnxCommunicationObject object;
            if (!manager.GetCommunicationObject(
                    properties["object_id"].value<std::string>(), object)) {
                throw std::runtime_error("Unknown KNX object ID");
            }
            return ObjectToJson(object, true);
        });

    server.AddTool("self.knx.read",
        "Requests an asynchronous KNX group read for a configured readable group address. Use this to refresh an unknown or stale status, then call self.knx.get_object for the cached response. This call does not wait for a bus response.",
        PropertyList({Property("group_address", kPropertyTypeString)}),
        [&manager](const PropertyList& properties) -> ReturnValue {
            const std::string address = properties["group_address"].value<std::string>();
            std::string error;
            ThrowOnFailure(manager.RequestRead(address, error), error);
            cJSON* json = cJSON_CreateObject();
            cJSON_AddBoolToObject(json, "requested", true);
            cJSON_AddStringToObject(json, "group_address", address.c_str());
            cJSON_AddStringToObject(json, "completion", "asynchronous");
            return json;
        });

    server.AddTool("self.knx.write",
        "Writes a configured writable KNX group address using its configured datapoint type. Use only for control when an object ID is unavailable; prefer self.knx.set_object. Value is a strict string: true/false for DPT-1, decimal integers for DPT-5/7/12/13, or finite decimal numbers for DPT-9/14.",
        PropertyList({
            Property("group_address", kPropertyTypeString),
            Property("value", kPropertyTypeString),
        }),
        [&manager](const PropertyList& properties) -> ReturnValue {
            std::string error;
            ThrowOnFailure(manager.WriteGroupAddress(
                properties["group_address"].value<std::string>(),
                properties["value"].value<std::string>(), error), error);
            return true;
        });

    server.AddTool("self.knx.set_object",
        "Controls one configured KNX communication object by semantic object ID. Use this for requests to switch devices, change numeric setpoints, or write other KNX values. Do not use it for status questions. Value is a strict string interpreted using the object's configured DPT.",
        PropertyList({
            Property("object_id", kPropertyTypeString),
            Property("value", kPropertyTypeString),
        }),
        [&manager](const PropertyList& properties) -> ReturnValue {
            std::string error;
            ThrowOnFailure(manager.WriteObject(
                properties["object_id"].value<std::string>(),
                properties["value"].value<std::string>(), error), error);
            return true;
        });
}