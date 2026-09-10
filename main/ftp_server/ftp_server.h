#pragma once

#include <esp_netif.h>

#include <cstdint>
#include <memory>
#include <mutex>

namespace espp {
class FtpServer;
}

class FtpServerManager {
public:
    static FtpServerManager& GetInstance();

    bool OnNetworkConnected(esp_netif_t* netif);
    void OnNetworkDisconnected();

private:
    FtpServerManager() = default;
    ~FtpServerManager();
    FtpServerManager(const FtpServerManager&) = delete;
    FtpServerManager& operator=(const FtpServerManager&) = delete;

    std::mutex mutex_;
    std::unique_ptr<espp::FtpServer> server_;
    uint32_t active_ipv4_ = 0;
};