#pragma once

#include <cstddef>
#include <mutex>

class LittleFsStorage {
public:
    static LittleFsStorage& GetInstance();

    bool Mount();
    void Unmount();
    bool Format();

    bool IsMounted() const;
    const char* GetMountPoint() const;
    size_t TotalBytes() const;
    size_t UsedBytes() const;

private:
    LittleFsStorage() = default;
    ~LittleFsStorage();
    LittleFsStorage(const LittleFsStorage&) = delete;
    LittleFsStorage& operator=(const LittleFsStorage&) = delete;

    bool EnsureRuntimeDirectories();
    bool RefreshUsage();

    mutable std::mutex mutex_;
    bool mounted_ = false;
    size_t total_bytes_ = 0;
    size_t used_bytes_ = 0;
};