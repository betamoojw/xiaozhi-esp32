#pragma once

#include "knx_object.h"

#include <esp_knx_ip/esp_knx_ip.h>
#include <esp_netif.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

enum class KnxServiceState {
    kDisabled,
    kWaitingForNetwork,
    kStarting,
    kRunning,
    kError,
};

class KnxManager {
public:
    static KnxManager& GetInstance();

    bool Initialize();
    void OnNetworkConnected(esp_netif_t* netif);
    void OnNetworkDisconnected();

    KnxServiceState GetState() const;
    const char* GetStateName() const;
    std::string GetLastError() const;
    std::string GetEndpoint() const;
    std::string GetPhysicalAddress() const;
    uint64_t GetLastCommunicationMs() const;
    size_t GetValidObjectCount() const;

    bool RegisterCommunicationObject(const KnxCommunicationObject& object,
                                     std::string& error);
    bool GetCommunicationObject(const std::string& id,
                                KnxCommunicationObject& object) const;
    bool GetCommunicationObjectByAddress(const std::string& group_address,
                                         KnxCommunicationObject& object) const;
    std::vector<KnxCommunicationObject> GetObjects(size_t offset, size_t limit) const;
    size_t GetObjectCount() const;

    bool RequestRead(const std::string& group_address, std::string& error);
    bool WriteObject(const std::string& id, const std::string& value,
                     std::string& error);
    bool WriteGroupAddress(const std::string& group_address,
                           const std::string& value, std::string& error);

private:
    static constexpr uint32_t kStartNotification = 1U << 0;
    static constexpr uint32_t kStopNotification = 1U << 1;

    KnxManager() = default;
    KnxManager(const KnxManager&) = delete;
    KnxManager& operator=(const KnxManager&) = delete;

    static void LifecycleTaskEntry(void* argument);
    static void TelegramCallback(const knx_telegram_t* telegram, void* context);
    void LifecycleTask();
    bool LoadConfiguration();
    bool StartTransport();
    bool StopTransport();
    bool StopTransportLocked();
    void HandleTelegram(const knx_telegram_t& telegram);
    bool Send(knx_address_t group_address, knx_command_t command,
              const uint8_t* data, size_t length, std::string& error);
    bool WriteObject(const KnxCommunicationObject& object,
                     const std::string& value, std::string& error);
    void SetState(KnxServiceState state, const std::string& error = "");

    mutable std::mutex mutex_;
    std::mutex transport_mutex_;
    std::vector<KnxCommunicationObject> objects_;
    KnxServiceState state_ = KnxServiceState::kDisabled;
    std::string last_error_;
    uint64_t last_communication_ms_ = 0;
    esp_netif_t* requested_netif_ = nullptr;
    esp_netif_t* active_netif_ = nullptr;
    uint32_t active_ipv4_ = 0;
    esp_knx_ip_handle_t handle_ = nullptr;
    TaskHandle_t lifecycle_task_ = nullptr;
    bool network_available_ = false;
    bool initialized_ = false;
    bool configuration_valid_ = false;
    bool force_restart_ = false;
};