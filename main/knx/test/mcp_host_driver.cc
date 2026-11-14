// Host-only driver for scripts/tests/test_knx_mcp_server.py. The real dispatcher,
// KNX manager, configuration parser and DPT codecs are linked without changes.
// No network sockets, filesystem writes or FreeRTOS worker tasks are started.
#include "knx_config.h"
#include "knx_manager.h"
#include "mcp_server.h"

#include <algorithm>
#include <cassert>
#include <iostream>
#include <map>

namespace {
struct SentTelegram {
    knx_address_t address;
    knx_command_t command;
    std::vector<uint8_t> data;
};
std::vector<SentTelegram> sent;
std::map<knx_address_t, std::pair<esp_knx_ip_telegram_callback_t, void*>> callbacks;
std::string persisted;
std::string upload;
bool fail_send = false;
bool fail_storage = false;
unsigned storage_writes = 0;
unsigned upload_reads = 0;

std::string Configuration() {
    CJsonUniquePtr root(cJSON_CreateObject());
    cJSON_AddStringToObject(root.get(), "media_type", "knx_ip");
    auto media = cJSON_AddObjectToObject(root.get(), "media_parameters");
    cJSON_AddStringToObject(media, "multicast_address", "224.0.23.12");
    cJSON_AddNumberToObject(media, "udp_port", 3671);
    cJSON_AddStringToObject(media, "transport_mode", "routing");
    cJSON_AddStringToObject(media, "interface_identifier", "host-test");
    cJSON_AddBoolToObject(media, "nat", false);
    cJSON_AddStringToObject(root.get(), "physical_address", "1.1.1");
    auto objects = cJSON_AddArrayToObject(root.get(), "comObjects");
    auto add = [&](const std::string& id, const char* dpt, bool readable, bool writable) {
        auto object = cJSON_CreateObject();
        cJSON_AddStringToObject(object, "id", id.c_str());
        cJSON_AddStringToObject(object, "name", id.c_str());
        auto address = "1/0/" + std::to_string(cJSON_GetArraySize(objects) + 1);
        cJSON_AddStringToObject(object, "group_address", address.c_str());
        cJSON_AddStringToObject(object, "datapoint_type", dpt);
        cJSON_AddBoolToObject(object, "readable", readable);
        cJSON_AddBoolToObject(object, "writable", writable);
        cJSON_AddItemToArray(objects, object);
    };
    add("switch", "DPT-1.001", true, true);
    add("temperature", "DPT-9.001", true, true);
    add("mode", "DPT-20.102", true, true);
    add("rgb", "DPT-232.600", true, true);
    add("label", "DPT-16.001", true, true);
    add("status", "DPT-1.001", true, false);
    add("command", "DPT-1.001", false, true);
    add("level", "DPT-5.001", true, true);
    for (int i = 0; i < 17; ++i) {
        add("extra." + std::to_string(i), "DPT-1.001", true, true);
    }
    CJsonStringUniquePtr text(cJSON_PrintUnformatted(root.get()));
    return text.get();
}

std::string Snapshot() {
    CJsonUniquePtr root(cJSON_CreateObject());
    auto messages = cJSON_AddArrayToObject(root.get(), "sent");
    for (const auto& telegram : sent) {
        auto item = cJSON_CreateObject();
        cJSON_AddNumberToObject(item, "address", telegram.address);
        cJSON_AddNumberToObject(item, "command", telegram.command);
        auto data = cJSON_AddArrayToObject(item, "data");
        for (auto byte : telegram.data) {
            cJSON_AddItemToArray(data, cJSON_CreateNumber(byte));
        }
        cJSON_AddItemToArray(messages, item);
    }
    cJSON_AddNumberToObject(root.get(), "storage_writes", storage_writes);
    cJSON_AddNumberToObject(root.get(), "upload_reads", upload_reads);
    cJSON_AddItemToObject(root.get(), "persisted", cJSON_Parse(persisted.c_str()));
    CJsonStringUniquePtr text(cJSON_PrintUnformatted(root.get()));
    return text.get();
}
}  // namespace

struct KnxManagerTestAccess {
    static bool Start(KnxManager& manager) { return manager.StartTransport(); }
};

int64_t esp_timer_get_time() { return 2000000; }
struct esp_knx_ip_context {
    esp_knx_ip_state_t state = ESP_KNX_IP_STATE_INITIALIZED;
};
extern "C" esp_err_t esp_knx_ip_create(const esp_knx_ip_config_t*, esp_knx_ip_handle_t* handle) {
    *handle = new esp_knx_ip_context;
    return ESP_OK;
}
extern "C" esp_err_t esp_knx_ip_destroy(esp_knx_ip_handle_t handle) {
    delete handle;
    callbacks.clear();
    return ESP_OK;
}
extern "C" esp_err_t esp_knx_ip_start(esp_knx_ip_handle_t handle) {
    handle->state = ESP_KNX_IP_STATE_RUNNING;
    return ESP_OK;
}
extern "C" esp_err_t esp_knx_ip_stop(esp_knx_ip_handle_t handle) {
    handle->state = ESP_KNX_IP_STATE_STOPPED;
    return ESP_OK;
}
extern "C" esp_err_t esp_knx_ip_get_state(esp_knx_ip_handle_t handle, esp_knx_ip_state_t* state) {
    *state = handle->state;
    return ESP_OK;
}
extern "C" esp_err_t esp_knx_ip_register_callback(esp_knx_ip_handle_t, knx_address_t address,
                                                  esp_knx_ip_telegram_callback_t callback,
                                                  void* context) {
    callbacks[address] = {callback, context};
    return ESP_OK;
}
extern "C" esp_err_t esp_knx_ip_send(esp_knx_ip_handle_t, knx_address_t address,
                                     knx_command_t command, const uint8_t* data, size_t length) {
    sent.push_back({address, command, std::vector<uint8_t>(data, data + length)});
    return fail_send ? ESP_FAIL : ESP_OK;
}
bool KnxLoadRuntimeConfiguration(std::string& json, bool& found, std::string&) {
    found = true;
    json = persisted;
    return true;
}
bool KnxLoadLegacyConfiguration(std::string&, bool& found, std::string&) {
    found = false;
    return true;
}
bool KnxLoadRuntimeConfigurationUpload(std::string& json, std::string&) {
    ++upload_reads;
    json = upload;
    return true;
}
bool KnxWriteRuntimeConfiguration(const std::string& json, std::string& error) {
    ++storage_writes;
    if (fail_storage) {
        error = "injected \"storage\" failure\nhost";
        return false;
    }
    persisted = json;
    return true;
}

int main(int argc, char** argv) {
    const std::string mode = argc > 2 ? argv[2] : "";
    persisted = Configuration();
    upload = mode == "bad-upload" ? "{" : persisted;
    fail_send = mode == "send-error";
    fail_storage = mode == "storage-error";
    auto& manager = KnxManager::GetInstance();
    assert(manager.Initialize());
    esp_netif_t netif;
    if (mode != "offline") {
        manager.OnNetworkConnected(&netif);
        assert(KnxManagerTestAccess::Start(manager));
    }

    auto& server = McpServer::GetInstance();
    // Exercise the same KNX registration entry points used during real startup.
    server.AddCommonTools();
    server.AddUserOnlyTools();
    if (mode == "pages") {
        for (int i = 0; i < 24; ++i) {
            server.AddTool("page." + std::to_string(i), std::string(700, 'x'), PropertyList(),
                           [](const PropertyList&) -> ToolResult { return true; });
        }
    }
    McpServer::ResponseSender sender;
    const bool custom_sender = argc > 1 && std::string(argv[1]) == "custom-sender";
    if (custom_sender) {
        sender = [](const std::string& payload) { std::cout << "custom\t" << payload << '\n'; };
    }
    std::string request;
    while (std::getline(std::cin, request)) {
        // @ commands are harness controls, never exposed as MCP tools. Tests use
        // them only to inspect fake side effects or inject a transport callback.
        if (request == "@receive") {
            knx_telegram_t telegram{};
            telegram.command = KNX_COMMAND_RESPONSE;
            telegram.destination = knx_group_address(1, 0, 6);
            telegram.data_length = 1;
            telegram.data[0] = 1;
            auto [callback, context] = callbacks.at(telegram.destination);
            callback(&telegram, context);
        } else if (request == "@drain") {
            Application::GetInstance().Drain();
        } else if (request != "@snapshot") {
            server.ParseMessage(request, sender);
            if (mode != "deferred") {
                Application::GetInstance().Drain();
            }
            continue;
        }
        std::cout << (custom_sender ? "custom\t" : "default\t") << Snapshot() << '\n';
    }
    Application::GetInstance().Drain();
}
