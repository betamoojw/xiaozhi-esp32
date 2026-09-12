#include "ftp_server.h"

#include <ftp_server.hpp>

#include <esp_littlefs.h>
#include <esp_log.h>

#include <sys/stat.h>
#include <cerrno>
#include <cstring>
#include <filesystem>

namespace {
constexpr char kTag[] = "FTP_SERVER";
constexpr char kLittleFsPartitionLabel[] = "littlefs";
constexpr char kLittleFsMountPoint[] = "/littlefs";

bool IsConfinedRoot(const std::filesystem::path& configured_root,
                    const std::filesystem::path& mount_point) {
    const auto root = configured_root.lexically_normal();
    const auto mount = mount_point.lexically_normal();
    if (!root.is_absolute() || root == mount) {
        return root == mount;
    }
    const auto relative = root.lexically_relative(mount);
    return !relative.empty() && !relative.is_absolute() &&
           *relative.begin() != std::filesystem::path("..");
}
}  // namespace

extern "C" esp_err_t xiaozhi_ftp_adopt_littlefs_mount(
    const esp_vfs_littlefs_conf_t* config) {
    if (config == nullptr || config->partition_label == nullptr ||
        std::strcmp(config->partition_label, kLittleFsPartitionLabel) != 0 ||
        !esp_littlefs_mounted(kLittleFsPartitionLabel)) {
        ESP_LOGE(kTag, "FTP filesystem wrapper requires the application-owned LittleFS mount");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGD(kTag, "FTP filesystem wrapper adopted the existing LittleFS mount");
    return ESP_OK;
}

FtpServerManager& FtpServerManager::GetInstance() {
    static FtpServerManager instance;
    return instance;
}

FtpServerManager::~FtpServerManager() { OnNetworkDisconnected(); }

bool FtpServerManager::OnNetworkConnected(esp_netif_t* netif) {
    esp_netif_ip_info_t ip_info = {};
    if (netif == nullptr || esp_netif_get_ip_info(netif, &ip_info) != ESP_OK ||
        ip_info.ip.addr == 0) {
        ESP_LOGE(kTag, "Cannot start FTP server without an active IPv4 interface");
        return false;
    }

    if (!esp_littlefs_mounted(kLittleFsPartitionLabel)) {
        ESP_LOGE(kTag, "Cannot start FTP server because LittleFS is not mounted");
        return false;
    }
    const std::filesystem::path configured_root(CONFIG_XIAOZHI_FTP_SERVER_ROOT);
    if (!IsConfinedRoot(configured_root, kLittleFsMountPoint)) {
        ESP_LOGE(kTag, "FTP root %s must be inside %s", CONFIG_XIAOZHI_FTP_SERVER_ROOT,
                 kLittleFsMountPoint);
        return false;
    }

    struct stat root_info = {};
    if (stat(CONFIG_XIAOZHI_FTP_SERVER_ROOT, &root_info) != 0) {
        ESP_LOGE(kTag, "FTP root %s is unavailable: %s", CONFIG_XIAOZHI_FTP_SERVER_ROOT,
                 std::strerror(errno));
        return false;
    }
    if (!S_ISDIR(root_info.st_mode)) {
        ESP_LOGE(kTag, "FTP root %s is not a directory", CONFIG_XIAOZHI_FTP_SERVER_ROOT);
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (server_ != nullptr && active_ipv4_ == ip_info.ip.addr) {
        return true;
    }
    if (server_ != nullptr) {
        server_->stop();
        server_.reset();
        active_ipv4_ = 0;
    }

    char ip_address[16] = {};
    if (esp_ip4addr_ntoa(&ip_info.ip, ip_address, sizeof(ip_address)) == nullptr) {
        ESP_LOGE(kTag, "Could not format the active IPv4 address");
        return false;
    }

    auto server = std::make_unique<espp::FtpServer>(ip_address, CONFIG_XIAOZHI_FTP_SERVER_PORT,
                                                    configured_root.lexically_normal());
    if (!server->start()) {
        ESP_LOGE(kTag, "Failed to start FTP server on port %d", CONFIG_XIAOZHI_FTP_SERVER_PORT);
        return false;
    }

    server_ = std::move(server);
    active_ipv4_ = ip_info.ip.addr;
    ESP_LOGW(kTag, "Unauthenticated FTP server listening at ftp://%s:%d, root=%s", ip_address,
             CONFIG_XIAOZHI_FTP_SERVER_PORT, CONFIG_XIAOZHI_FTP_SERVER_ROOT);
    return true;
}

void FtpServerManager::OnNetworkDisconnected() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (server_ == nullptr) {
        return;
    }

    server_->stop();
    server_.reset();
    active_ipv4_ = 0;
    ESP_LOGI(kTag, "FTP server stopped");
}