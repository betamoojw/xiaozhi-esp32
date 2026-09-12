#include "knx_manager.h"

#include "knx_config.h"

#include <esp_log.h>
#include <esp_timer.h>
#include <sdkconfig.h>

#include <algorithm>
#include <set>

constexpr char kTag[] = "KNX_MGR";

KnxManager& KnxManager::GetInstance() {
    static KnxManager instance;
    return instance;
}

bool KnxManager::Initialize() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (initialized_) {
        return true;
    }
    initialized_ = true;
    state_ = KnxServiceState::kWaitingForNetwork;

    configuration_valid_ = LoadConfiguration();
    if (!configuration_valid_) {
        state_ = KnxServiceState::kError;
    }
    if (xTaskCreate(LifecycleTaskEntry, "knx_lifecycle", 4096, this, 3, &lifecycle_task_) !=
        pdPASS) {
        lifecycle_task_ = nullptr;
        state_ = KnxServiceState::kError;
        last_error_ = "Could not create KNX lifecycle task";
        return false;
    }
    ESP_LOGI(kTag, "Loaded %u KNX communication objects", static_cast<unsigned>(objects_.size()));
    return state_ != KnxServiceState::kError;
}

bool KnxManager::LoadConfiguration() {
    std::string json_text;
    bool runtime_configuration_found = false;
    if (!KnxLoadRuntimeConfiguration(json_text, runtime_configuration_found, last_error_)) {
        ESP_LOGE(kTag, "%s", last_error_.c_str());
        return false;
    }

    bool initialize_runtime_configuration = false;
    if (runtime_configuration_found) {
        ESP_LOGI(kTag, "Loading KNX configuration from LittleFS");
    } else {
        bool legacy_configuration_found = false;
        if (!KnxLoadLegacyConfiguration(json_text, legacy_configuration_found, last_error_)) {
            ESP_LOGE(kTag, "%s", last_error_.c_str());
            return false;
        }
        initialize_runtime_configuration = true;
        if (legacy_configuration_found) {
            ESP_LOGI(kTag, "Migrating legacy KNX configuration from NVS to LittleFS");
        } else {
            ESP_LOGI(kTag, "Initializing an empty KNX configuration in LittleFS");
            json_text = "[]";
        }
    }
    std::vector<KnxCommunicationObject> objects;
    std::string canonical_json;

    if (!KnxParseConfiguration(json_text, CONFIG_XIAOZHI_KNX_IP_MAX_OBJECTS,
                               CONFIG_ESP_KNX_IP_MAX_GROUP_ADDRESSES, objects, canonical_json,
                               last_error_)) {
        ESP_LOGE(kTag, "%s", last_error_.c_str());
        return false;
    }
    if (initialize_runtime_configuration &&
        !KnxWriteRuntimeConfiguration(canonical_json, last_error_)) {
        ESP_LOGE(kTag, "Could not initialize KNX runtime configuration: %s", last_error_.c_str());
        return false;
    }
    objects_ = std::move(objects);
    return true;
}

bool KnxManager::RegisterCommunicationObject(const KnxCommunicationObject& object,
                                             std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto duplicate_id =
        std::find_if(objects_.begin(), objects_.end(),
                     [&object](const auto& existing) { return existing.id == object.id; });
    if (duplicate_id != objects_.end()) {
        error = "Duplicate KNX object ID: " + object.id;
        return false;
    }
    const auto duplicate_address =
        std::find_if(objects_.begin(), objects_.end(), [&object](const auto& existing) {
            return existing.parsed_group_address == object.parsed_group_address;
        });
    if (duplicate_address != objects_.end()) {
        error = "Duplicate KNX group address: " + object.group_address;
        return false;
    }
    objects_.push_back(object);
    return true;
}

bool KnxManager::ImportConfiguration(const std::string& json_text, size_t& object_count,
                                     std::string& error) {
    std::lock_guard<std::mutex> import_lock(import_mutex_);
    std::vector<KnxCommunicationObject> objects;
    std::string canonical_json;
    if (!KnxParseConfiguration(json_text, CONFIG_XIAOZHI_KNX_IP_MAX_OBJECTS,
                               CONFIG_ESP_KNX_IP_MAX_GROUP_ADDRESSES, objects, canonical_json,
                               error)) {
        return false;
    }

    if (!KnxWriteRuntimeConfiguration(canonical_json, error)) {
        ESP_LOGE(kTag, "%s", error.c_str());
        return false;
    }
    ESP_LOGI(kTag, "Persisted KNX configuration to LittleFS");

    bool network_available = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        objects_ = std::move(objects);
        object_count = objects_.size();
        configuration_valid_ = true;
        last_error_.clear();
        force_restart_ = true;
        network_available = network_available_ && requested_netif_ != nullptr;
        state_ =
            network_available ? KnxServiceState::kStarting : KnxServiceState::kWaitingForNetwork;
    }
    if (network_available && lifecycle_task_ != nullptr) {
        xTaskNotify(lifecycle_task_, kStartNotification, eSetBits);
    }
    ESP_LOGI(kTag, "Imported and persisted %u KNX communication objects",
             static_cast<unsigned>(object_count));
    return true;
}

void KnxManager::OnNetworkConnected(esp_netif_t* netif) {
    esp_netif_ip_info_t ip_info = {};
    const bool have_ip = netif != nullptr && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK &&
                         ip_info.ip.addr != 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        requested_netif_ = netif;
        network_available_ = have_ip;
        if (have_ip && active_netif_ == netif && active_ipv4_ != 0 &&
            active_ipv4_ != ip_info.ip.addr) {
            force_restart_ = true;
        }
        if (!configuration_valid_) {
            state_ = KnxServiceState::kError;
        } else if (!have_ip) {
            state_ = KnxServiceState::kError;
            last_error_ = netif == nullptr ? "Active network does not expose an ESP-IDF netif"
                                           : "Active network does not have an IPv4 address";
        }
    }
    if (lifecycle_task_ != nullptr && have_ip && configuration_valid_) {
        xTaskNotify(lifecycle_task_, kStartNotification, eSetBits);
    }
}

void KnxManager::OnNetworkDisconnected() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        network_available_ = false;
        requested_netif_ = nullptr;
    }
    if (lifecycle_task_ != nullptr) {
        xTaskNotify(lifecycle_task_, kStopNotification, eSetBits);
    }
}

void KnxManager::LifecycleTaskEntry(void* argument) {
    static_cast<KnxManager*>(argument)->LifecycleTask();
}

void KnxManager::LifecycleTask() {
    bool retry = false;
    while (true) {
        uint32_t notifications = 0;
        const TickType_t timeout =
            retry ? pdMS_TO_TICKS(CONFIG_XIAOZHI_KNX_IP_RECONNECT_INTERVAL_MS) : portMAX_DELAY;
        const BaseType_t notified = xTaskNotifyWait(0, UINT32_MAX, &notifications, timeout);

        bool network_available = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            network_available =
                configuration_valid_ && network_available_ && requested_netif_ != nullptr;
        }
        if (!network_available) {
            retry = !StopTransport();
            if (!retry && configuration_valid_) {
                SetState(KnxServiceState::kWaitingForNetwork);
            }
        } else if (notified == pdFALSE || notifications != 0) {
            retry = !StartTransport();
        }
    }
}

bool KnxManager::StartTransport() {
    esp_netif_t* netif = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        netif = requested_netif_;
        state_ = KnxServiceState::kStarting;
        last_error_.clear();
    }
    if (netif == nullptr) {
        SetState(KnxServiceState::kWaitingForNetwork);
        return false;
    }

    std::lock_guard<std::mutex> transport_lock(transport_mutex_);
    bool force_restart = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        force_restart = force_restart_;
        force_restart_ = false;
    }
    if (!force_restart && handle_ != nullptr && active_netif_ == netif) {
        esp_knx_ip_state_t component_state;
        if (esp_knx_ip_get_state(handle_, &component_state) == ESP_OK &&
            component_state == ESP_KNX_IP_STATE_RUNNING) {
            SetState(KnxServiceState::kRunning);
            return true;
        }
    }
    if (handle_ != nullptr) {
        if (!StopTransportLocked()) {
            return false;
        }
    }

    knx_address_t physical_address = 0;
    if (!KnxParsePhysicalAddress(CONFIG_XIAOZHI_KNX_IP_PHYSICAL_ADDRESS, physical_address)) {
        SetState(KnxServiceState::kError, "Invalid configured KNX physical address");
        return false;
    }
    esp_knx_ip_config_t config = ESP_KNX_IP_CONFIG_DEFAULT(netif);
    config.physical_address = physical_address;
    config.multicast_address = CONFIG_XIAOZHI_KNX_IP_MULTICAST_ADDRESS;
    config.port = CONFIG_XIAOZHI_KNX_IP_PORT;
    config.load_physical_address_from_nvs = false;
    esp_err_t result = esp_knx_ip_create(&config, &handle_);
    if (result != ESP_OK) {
        SetState(KnxServiceState::kError,
                 std::string("KNX create failed: ") + esp_err_to_name(result));
        return false;
    }
    active_netif_ = netif;

    std::set<knx_address_t> registered_addresses;
    std::vector<knx_address_t> addresses;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& object : objects_) {
            if (registered_addresses.insert(object.parsed_group_address).second) {
                addresses.push_back(object.parsed_group_address);
            }
        }
    }
    for (const auto address : addresses) {
        result = esp_knx_ip_register_callback(handle_, address, TelegramCallback, this);
        if (result != ESP_OK) {
            break;
        }
    }
    if (result == ESP_OK) {
        result = esp_knx_ip_start(handle_);
    }
    if (result != ESP_OK) {
        esp_knx_ip_destroy(handle_);
        handle_ = nullptr;
        active_netif_ = nullptr;
        active_ipv4_ = 0;
        SetState(KnxServiceState::kError,
                 std::string("KNX start failed: ") + esp_err_to_name(result));
        return false;
    }

    esp_netif_ip_info_t ip_info = {};
    esp_netif_get_ip_info(netif, &ip_info);
    active_ipv4_ = ip_info.ip.addr;
    SetState(KnxServiceState::kRunning);
    ESP_LOGI(kTag, "KNX routing started on %s:%d", CONFIG_XIAOZHI_KNX_IP_MULTICAST_ADDRESS,
             CONFIG_XIAOZHI_KNX_IP_PORT);
    return true;
}

bool KnxManager::StopTransport() {
    std::lock_guard<std::mutex> transport_lock(transport_mutex_);
    return StopTransportLocked();
}

bool KnxManager::StopTransportLocked() {
    if (handle_ == nullptr) {
        return true;
    }
    esp_knx_ip_state_t component_state;
    esp_err_t result = esp_knx_ip_get_state(handle_, &component_state);
    if (result != ESP_OK) {
        SetState(KnxServiceState::kError,
                 std::string("KNX state query failed: ") + esp_err_to_name(result));
        return false;
    }
    if (component_state == ESP_KNX_IP_STATE_RUNNING) {
        result = esp_knx_ip_stop(handle_);
        if (result != ESP_OK) {
            SetState(KnxServiceState::kError,
                     std::string("KNX stop failed: ") + esp_err_to_name(result));
            return false;
        }
    }
    result = esp_knx_ip_destroy(handle_);
    if (result != ESP_OK) {
        SetState(KnxServiceState::kError,
                 std::string("KNX destroy failed: ") + esp_err_to_name(result));
        return false;
    }
    handle_ = nullptr;
    active_netif_ = nullptr;
    active_ipv4_ = 0;
    return true;
}

void KnxManager::TelegramCallback(const knx_telegram_t* telegram, void* context) {
    if (telegram != nullptr && context != nullptr) {
        static_cast<KnxManager*>(context)->HandleTelegram(*telegram);
    }
}

void KnxManager::HandleTelegram(const knx_telegram_t& telegram) {
    if (telegram.command != KNX_COMMAND_RESPONSE && telegram.command != KNX_COMMAND_WRITE) {
        return;
    }
    const uint64_t timestamp = esp_timer_get_time() / 1000;
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& object : objects_) {
        if (object.parsed_group_address != telegram.destination) {
            continue;
        }
        KnxValue value;
        if (KnxDecodeValue(object.datapoint_type, telegram.data, telegram.data_length, value)) {
            object.current_value = value;
            object.valid = true;
            object.last_update_ms = timestamp;
            last_communication_ms_ = timestamp;
#if CONFIG_XIAOZHI_KNX_IP_DEBUG
            ESP_LOGI(kTag, "Received %s %s=%s", object.group_address.c_str(),
                     KnxDptName(object.datapoint_type), KnxValueToString(value).c_str());
#endif
        } else {
            last_error_ = "Could not decode KNX value for " + object.id;
            ESP_LOGW(kTag, "%s", last_error_.c_str());
        }
    }
}

bool KnxManager::Send(knx_address_t group_address, knx_command_t command, const uint8_t* data,
                      size_t length, std::string& error) {
    std::lock_guard<std::mutex> transport_lock(transport_mutex_);
    if (handle_ == nullptr) {
        error = "KNX routing is not running";
        return false;
    }
    const esp_err_t result = esp_knx_ip_send(handle_, group_address, command, data, length);
    if (result != ESP_OK) {
        error = std::string("KNX send failed: ") + esp_err_to_name(result);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            state_ = KnxServiceState::kError;
            last_error_ = error;
            force_restart_ = true;
        }
        if (lifecycle_task_ != nullptr) {
            xTaskNotify(lifecycle_task_, kStartNotification, eSetBits);
        }
        return false;
    }
    return true;
}

bool KnxManager::ImportConfigurationFile(size_t& object_count, std::string& error) {
    std::string json_text;
    if (!KnxLoadRuntimeConfigurationUpload(json_text, error)) {
        return false;
    }
    if (!ImportConfiguration(json_text, object_count, error)) {
        return false;
    }
    return true;
}

bool KnxManager::RequestRead(const std::string& group_address, std::string& error) {
    KnxCommunicationObject object;
    if (!GetCommunicationObjectByAddress(group_address, object)) {
        error = "Unknown KNX group address";
        return false;
    }
    if (!object.readable) {
        error = "KNX communication object is not readable";
        return false;
    }
    const uint8_t apdu = 0;
    return Send(object.parsed_group_address, KNX_COMMAND_READ, &apdu, 1, error);
}

bool KnxManager::WriteObject(const std::string& id, const std::string& value, std::string& error) {
    KnxCommunicationObject object;
    if (!GetCommunicationObject(id, object)) {
        error = "Unknown KNX object ID";
        return false;
    }
    return WriteObject(object, value, error);
}

bool KnxManager::WriteGroupAddress(const std::string& group_address, const std::string& value,
                                   std::string& error) {
    KnxCommunicationObject object;
    if (!GetCommunicationObjectByAddress(group_address, object)) {
        error = "Unknown KNX group address";
        return false;
    }
    return WriteObject(object, value, error);
}

bool KnxManager::WriteObject(const KnxCommunicationObject& object, const std::string& text,
                             std::string& error) {
    if (!object.writable) {
        error = "KNX communication object is not writable";
        return false;
    }
    KnxValue value;
    if (!KnxParseValue(object.datapoint_type, text, value)) {
        error = std::string("Invalid value for ") + KnxDptName(object.datapoint_type);
        return false;
    }
    std::vector<uint8_t> encoded;
    if (!KnxEncodeValue(object.datapoint_type, value, encoded)) {
        error = std::string("Value is outside the supported range for ") +
                KnxDptName(object.datapoint_type);
        return false;
    }
    return Send(object.parsed_group_address, KNX_COMMAND_WRITE, encoded.data(), encoded.size(),
                error);
}

bool KnxManager::GetCommunicationObject(const std::string& id,
                                        KnxCommunicationObject& object) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = std::find_if(objects_.begin(), objects_.end(),
                                    [&id](const auto& candidate) { return candidate.id == id; });
    if (found == objects_.end()) {
        return false;
    }
    object = *found;
    return true;
}

bool KnxManager::GetCommunicationObjectByAddress(const std::string& group_address,
                                                 KnxCommunicationObject& object) const {
    knx_address_t parsed_address = 0;
    if (!KnxParseGroupAddress(group_address, parsed_address)) {
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found =
        std::find_if(objects_.begin(), objects_.end(), [parsed_address](const auto& candidate) {
            return candidate.parsed_group_address == parsed_address;
        });
    if (found == objects_.end()) {
        return false;
    }
    object = *found;
    return true;
}

std::vector<KnxCommunicationObject> KnxManager::GetObjects(size_t offset, size_t limit) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (offset >= objects_.size()) {
        return {};
    }
    const size_t end = std::min(objects_.size(), offset + limit);
    return {objects_.begin() + offset, objects_.begin() + end};
}

size_t KnxManager::GetObjectCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return objects_.size();
}

size_t KnxManager::GetValidObjectCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return std::count_if(objects_.begin(), objects_.end(),
                         [](const auto& object) { return object.valid; });
}

KnxServiceState KnxManager::GetState() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}

const char* KnxManager::GetStateName() const {
    switch (GetState()) {
        case KnxServiceState::kDisabled:
            return "disabled";
        case KnxServiceState::kWaitingForNetwork:
            return "waiting_for_network";
        case KnxServiceState::kStarting:
            return "starting";
        case KnxServiceState::kRunning:
            return "running";
        case KnxServiceState::kError:
            return "error";
    }
    return "unknown";
}

std::string KnxManager::GetLastError() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return last_error_;
}

std::string KnxManager::GetEndpoint() const {
    return std::string(CONFIG_XIAOZHI_KNX_IP_MULTICAST_ADDRESS) + ":" +
           std::to_string(CONFIG_XIAOZHI_KNX_IP_PORT);
}

std::string KnxManager::GetPhysicalAddress() const {
    knx_address_t address = 0;
    return KnxParsePhysicalAddress(CONFIG_XIAOZHI_KNX_IP_PHYSICAL_ADDRESS, address)
               ? KnxFormatPhysicalAddress(address)
               : "invalid";
}

uint64_t KnxManager::GetLastCommunicationMs() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return last_communication_ms_;
}

void KnxManager::SetState(KnxServiceState state, const std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_ = state;
    if (!error.empty()) {
        last_error_ = error;
        ESP_LOGW(kTag, "%s", error.c_str());
    } else if (state == KnxServiceState::kRunning) {
        last_error_.clear();
    }
}