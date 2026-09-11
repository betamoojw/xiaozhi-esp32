#include "littlefs_storage.h"

#include <esp_err.h>
#include <esp_littlefs.h>
#include <esp_log.h>
#include <esp_partition.h>

#include <sys/stat.h>
#include <cerrno>
#include <cstring>

namespace {
constexpr char kTag[] = "LITTLEFS";
constexpr char kPartitionLabel[] = "lfs";
constexpr char kMountPoint[] = "/littlefs";
constexpr char kInterfacesDirectory[] = "/littlefs/interfaces";
}  // namespace

LittleFsStorage& LittleFsStorage::GetInstance() {
    static LittleFsStorage instance;
    return instance;
}

LittleFsStorage::~LittleFsStorage() { Unmount(); }

bool LittleFsStorage::Mount() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (mounted_) {
        return true;
    }

    const esp_partition_t* partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_LITTLEFS, kPartitionLabel);
    if (partition == nullptr) {
        ESP_LOGE(kTag, "LittleFS partition %s not found", kPartitionLabel);
        return false;
    }

    const esp_vfs_littlefs_conf_t config = {
        .base_path = kMountPoint,
        .partition_label = kPartitionLabel,
        .partition = nullptr,
        .format_if_mount_failed = false,
        .read_only = false,
        .dont_mount = false,
        .grow_on_mount = false,
    };
    const esp_err_t result = esp_vfs_littlefs_register(&config);
    if (result != ESP_OK) {
        ESP_LOGE(kTag, "Failed to mount LittleFS partition=%s mount=%s error=%s", kPartitionLabel,
                 kMountPoint, esp_err_to_name(result));
        return false;
    }

    mounted_ = true;
    if (!EnsureRuntimeDirectories() || !RefreshUsage()) {
        esp_vfs_littlefs_unregister(kPartitionLabel);
        mounted_ = false;
        total_bytes_ = 0;
        used_bytes_ = 0;
        return false;
    }

    ESP_LOGI(kTag, "LittleFS mounted partition=%s mount=%s total=%u used=%u free=%u",
             kPartitionLabel, kMountPoint, static_cast<unsigned>(total_bytes_),
             static_cast<unsigned>(used_bytes_), static_cast<unsigned>(total_bytes_ - used_bytes_));
    return true;
}

void LittleFsStorage::Unmount() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!mounted_) {
        return;
    }
    const esp_err_t result = esp_vfs_littlefs_unregister(kPartitionLabel);
    if (result != ESP_OK) {
        ESP_LOGE(kTag, "Failed to unmount LittleFS partition=%s error=%s", kPartitionLabel,
                 esp_err_to_name(result));
        return;
    }
    mounted_ = false;
    total_bytes_ = 0;
    used_bytes_ = 0;
}

bool LittleFsStorage::Format() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (mounted_) {
        ESP_LOGE(kTag, "Refusing to format mounted LittleFS partition=%s", kPartitionLabel);
        return false;
    }
    const esp_err_t result = esp_littlefs_format(kPartitionLabel);
    if (result != ESP_OK) {
        ESP_LOGE(kTag, "Failed to format LittleFS partition=%s error=%s", kPartitionLabel,
                 esp_err_to_name(result));
        return false;
    }
    ESP_LOGW(kTag, "Formatted LittleFS partition=%s", kPartitionLabel);
    return true;
}

bool LittleFsStorage::IsMounted() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return mounted_;
}

const char* LittleFsStorage::GetMountPoint() const { return kMountPoint; }

size_t LittleFsStorage::TotalBytes() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return total_bytes_;
}

size_t LittleFsStorage::UsedBytes() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return used_bytes_;
}

bool LittleFsStorage::EnsureRuntimeDirectories() {
    struct stat info = {};
    if (stat(kInterfacesDirectory, &info) == 0) {
        if (S_ISDIR(info.st_mode)) {
            return true;
        }
        ESP_LOGE(kTag, "%s exists but is not a directory", kInterfacesDirectory);
        return false;
    }
    if (errno != ENOENT || mkdir(kInterfacesDirectory, 0755) != 0) {
        ESP_LOGE(kTag, "Failed to create %s: %s", kInterfacesDirectory, std::strerror(errno));
        return false;
    }
    return true;
}

bool LittleFsStorage::RefreshUsage() {
    const esp_err_t result = esp_littlefs_info(kPartitionLabel, &total_bytes_, &used_bytes_);
    if (result != ESP_OK || used_bytes_ > total_bytes_) {
        ESP_LOGE(kTag, "Failed to query LittleFS partition=%s error=%s", kPartitionLabel,
                 esp_err_to_name(result));
        return false;
    }
    return true;
}