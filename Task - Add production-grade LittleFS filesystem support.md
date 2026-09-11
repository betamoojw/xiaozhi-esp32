# Task: Add production-grade LittleFS filesystem support to `feat/knx_ip`

Repository:

`https://github.com/betamoojw/xiaozhi-esp32`

Target branch:

`feat/knx_ip`

## Objective

Add a production-grade, writable LittleFS filesystem to the existing `feat/knx_ip` branch.

The LittleFS filesystem will provide persistent runtime/user storage and will be used by the existing FTP server.

The primary runtime filesystem configuration must be:

```text
LittleFS partition label:
littlefs

LittleFS VFS mount point:
/littlefs
```

This filesystem is intended for files such as:

```text
/littlefs/interfaces/knxConfig.json
```

and future runtime/user-managed files.

---

# 1. CRITICAL STORAGE ARCHITECTURE

The branch currently has an existing XiaoZhi Assets filesystem.

Do NOT replace or modify the existing Assets storage mechanism.

There must be two independent filesystems/storage mechanisms:

```text
ESP32 Flash
│
├── assets
│   └── existing XiaoZhi Assets / SPIFFS
│
└── littlefs
    └── LittleFS
        └── mounted at /littlefs
```

The final architecture must be:

```text
Partition label     Filesystem       VFS / API
---------------------------------------------------------
assets              existing SPIFFS  XiaoZhi Assets
littlefs                 LittleFS         /littlefs
```

The `assets` partition remains dedicated to packaged firmware assets.

The `littlefs` partition is the writable runtime filesystem.

---

# 2. DO NOT USE `assets` FOR LITTLEFS

The existing branch contains assets such as:

```text
main/assets/
    common/
    interfaces/
    locales/
```

and existing code accesses the `assets` partition through the XiaoZhi Assets subsystem.

Do NOT:

- rename `assets`;
- replace `assets` with LittleFS;
- mount LittleFS on `assets`;
- change the Assets partition filesystem;
- change `Assets` to use LittleFS;
- expose `assets/interfaces` as a writable directory;
- use `assets/interfaces/knxConfig.json` as a writable runtime file.

The existing Assets system must continue working unchanged.

---

# 3. LITTLEFS PARTITION LABEL — MUST BE `littlefs`

The new partition MUST use exactly:

```text
littlefs
```

as its partition label.

Do not use:

```text
storage
littlefs
data
filesystem
```

as the partition label.

The distinction must remain:

```text
partition label = littlefs
mount point     = /littlefs
```

For example, the LittleFS registration must conceptually use:

```cpp
.partition_label = "littlefs"
.base_path = "/littlefs"
```

Use the exact API structure required by the installed LittleFS component.

Do not confuse:

```text
littlefs
```

with:

```text
/littlefs
```

They are intentionally different.

---

# 4. ADD LITTLEFS PARTITION

Add a dedicated data partition:

```text
littlefs, data, littlefs, <offset>, <size>
```

Use the correct LittleFS partition subtype supported by the selected LittleFS component.

Verify the actual partition subtype supported by the project's ESP-IDF/LittleFS version before modifying the CSV.

Do not guess the subtype.

The resulting partition table must contain both:

```text
assets
littlefs
```

with no overlap.

---

# 5. DETERMINE THE CORRECT PARTITION SIZE

Inspect all existing partition layouts under:

```text
partitions/
```

especially:

```text
partitions/v1/
partitions/v2/
```

Determine which layouts are actively used.

For the standard 16 MB configuration, the current layout contains approximately:

```text
nvs
otadata
phy_init
ota_0
ota_1
assets
```

The existing `assets` partition currently occupies approximately 8 MB.

There is not enough unused flash for a new partition without changing the layout.

Therefore:

1. Measure the actual generated Assets image.
2. Determine the minimum safe Assets partition size.
3. Preserve sufficient space for OTA images.
4. Allocate the remaining safe space to:

```text
littlefs
```

A candidate layout is:

```text
assets = 6 MB
littlefs    = 2 MB
```

but ONLY use this if the actual Assets image is proven to fit inside 6 MB.

Do not blindly shrink `assets`.

Do not blindly use 2 MB for `littlefs`.

Do not shrink OTA partitions unless absolutely necessary.

Document the exact final offsets and sizes.

---

# 6. LITTLEFS COMPONENT

Use the ESP-IDF managed component:

```text
joltwallet/littlefs
```

Prefer a stable version compatible with the project's ESP-IDF version.

Currently the expected stable version is:

```text
1.22.3
```

Verify compatibility with the repository's actual ESP-IDF version before implementation.

Add the dependency using the project's existing dependency-management convention.

For example, inspect:

```text
main/idf_component.yml
```

before modifying it.

Do not vendor LittleFS manually into the repository.

---

# 7. LITTLEFS MOUNT POINT

Use:

```text
/littlefs
```

as the VFS mount point.

Therefore:

```text
partition label = littlefs
mount point     = /littlefs
```

Examples:

```text
/littlefs/interfaces
/littlefs/interfaces/knxConfig.json
/littlefs/config
/littlefs/uploads
```

Do NOT mount LittleFS at:

```text
/
/assets
/storage
/littlefs
```

unless there is a compelling existing architectural requirement.

The required default is:

```text
/littlefs
```

---

# 8. LITTLEFS INITIALIZATION

Create a dedicated filesystem manager consistent with the project's existing architecture.

Recommended:

```text
main/storage/littlefs_storage.h
main/storage/littlefs_storage.cc
```

A suitable interface may be:

```cpp
class LittleFsStorage {
public:
    static LittleFsStorage& GetInstance();

    bool Mount();
    void Unmount();

    bool IsMounted() const;

    const char* GetMountPoint() const;

    bool Format();

    size_t TotalBytes() const;
    size_t UsedBytes() const;
};
```

The exact interface may be adjusted to match the repository architecture.

The implementation must always use:

```text
partition_label = littlefs
mount_point     = /littlefs
```

---

# 9. MOUNT CONFIGURATION

Use the actual LittleFS API provided by the installed component.

The configuration should conceptually be:

```cpp
esp_vfs_littlefs_conf_t conf = {
    .base_path = "/littlefs",
    .partition_label = "littlefs",
    .format_if_mount_failed = false,
    .read_only = false,
};
```

Use the exact field ordering/types/API supported by the actual component version.

Verify against the installed dependency instead of copying an obsolete example.

---

# 10. NEVER AUTO-FORMAT ON BOOT

Production firmware MUST NOT silently erase the user's filesystem.

Use:

```text
format_if_mount_failed = false
```

If mounting fails:

- log the error;
- do not format;
- do not erase the `littlefs` partition;
- mark LittleFS unavailable;
- allow the rest of the firmware to continue where possible.

Formatting must be an explicit administrative operation.

---

# 11. STARTUP ORDER

LittleFS must be mounted before components that use runtime files.

Inspect:

```text
main/main.cc
main/application.cc
main/application.h
main/settings.cc
main/settings.h
main/ftp_server/*
main/knx/*
```

Determine the correct initialization point.

Required startup sequence:

```text
Application startup
       │
       ▼
Mount LittleFS
partition = littlefs
mount     = /littlefs
       │
       ▼
Create /littlefs/interfaces if required
       │
       ▼
Start network services
       │
       ▼
Start FTP server
       │
       ▼
KNX runtime file operations
```

Do not mount/unmount LittleFS for every file operation.

Keep it mounted for the application lifetime.

---

# 12. LOGGING

On successful mount, log:

```text
LittleFS mounted
partition=littlefs
mount=/littlefs
total=<bytes>
used=<bytes>
free=<bytes>
```

On failure:

```text
Failed to mount LittleFS
partition=littlefs
mount=/littlefs
error=<error>
```

Use the project's existing logging conventions.

---

# 13. CREATE KNX DIRECTORY

After LittleFS mounts successfully, ensure:

```text
/littlefs/interfaces
```

exists.

The operation must be idempotent.

Do not create or modify:

```text
/assets/interfaces
```

The factory asset remains:

```text
assets/interfaces/knxConfig.json
```

while the writable runtime filesystem contains:

```text
/littlefs/interfaces/knxConfig.json
```

---

# 14. FTP SERVER INTEGRATION

Inspect the existing FTP implementation:

```text
main/ftp_server/ftp_server.cc
main/ftp_server/ftp_server.h
```

The existing implementation uses:

```cpp
stat(CONFIG_XIAOZHI_FTP_SERVER_ROOT, ...)
```

and passes the configured root to:

```cpp
espp::FtpServer(...)
```

Modify it so the default FTP root is:

```text
/littlefs
```

The FTP server must therefore operate against the LittleFS VFS.

The FTP root must NOT be:

```text
/assets
/assets/interfaces
```

or any path on the Assets partition.

---

# 15. FTP ROOT CONFIGURATION

Inspect:

```text
CONFIG_XIAOZHI_FTP_SERVER
CONFIG_XIAOZHI_FTP_SERVER_ROOT
CONFIG_XIAOZHI_FTP_SERVER_PORT
```

Preserve the existing FTP configuration architecture.

Set the default root to:

```text
/littlefs
```

If configurable, validate that the configured root is inside the LittleFS mount.

For example:

```text
/littlefs
/littlefs/interfaces
```

are valid.

Arbitrary physical paths must not be accepted.

---

# 16. FTP PATH TRAVERSAL PROTECTION

Because FTP allows remote filesystem operations, ensure that a client cannot escape:

```text
/littlefs
```

Reject:

```text
../
../../
/assets
/etc
```

and equivalent traversal attempts.

Normalize paths before filesystem operations.

Do not rely only on a simple string-prefix check.

The FTP root must remain confined to the LittleFS filesystem.

---

# 17. FILE OPERATIONS

LittleFS must support normal POSIX/VFS operations:

```text
open
read
write
close
stat
mkdir
rmdir
rename
unlink
```

The FTP server should therefore be able to support, where provided by the existing FTP library:

```text
LIST
RETR
STOR
DELE
RNFR
RNTO
MKD
RMD
```

Do not rewrite the FTP protocol implementation unnecessarily.

Reuse the existing FTP library.

---

# 18. CONCURRENCY

Inspect how the existing FTP server and KNX tasks access files.

If multiple tasks can access LittleFS concurrently, protect filesystem operations with an appropriate FreeRTOS mutex.

Protect operations such as:

```text
read
write
rename
unlink
mkdir
rmdir
```

Do not hold a filesystem mutex across long network operations.

Do not unnecessarily serialize NVS or Assets operations.

---

# 19. KNX CONFIGURATION MODEL

The storage model must be:

```text
Factory/default configuration:

Assets
└── interfaces/knxConfig.json


Runtime/user configuration:

LittleFS
└── /littlefs/interfaces/knxConfig.json
```

The factory asset is not writable.

The runtime file is writable.

If the existing KNX implementation persists authoritative runtime configuration in NVS, preserve that architecture unless explicitly required otherwise.

Do not silently replace NVS with LittleFS.

LittleFS should provide the persistent file-based interface required for FTP and external configuration.

---

# 20. KNX FILE IMPORT

If the existing `feat/knx_ip` branch contains KNX file import/export logic, integrate it with:

```text
/littlefs/interfaces/knxConfig.json
```

Reuse existing validation logic.

Do not duplicate:

```text
KnxConfig
KnxManager
self.knx.import_configuration
```

validation.

An uploaded configuration must be:

```text
FTP upload
    ↓
temporary file
    ↓
complete write
    ↓
JSON parse
    ↓
KNX validation
    ↓
atomic rename
    ↓
activate/persist according to existing KNX architecture
```

Never activate an invalid uploaded file.

---

# 21. ATOMIC CONFIGURATION WRITES

For:

```text
/littlefs/interfaces/knxConfig.json
```

do not directly overwrite the active file.

Use:

```text
/littlefs/interfaces/knxConfig.json.tmp
```

then:

```text
write
flush/close
validate
rename
```

Only replace the active configuration after the complete file has been written and validated.

Account for the exact rename semantics of the installed LittleFS implementation.

The objective is to avoid leaving a partially-written configuration after power loss.

---

# 22. CMAKE

Inspect:

```text
CMakeLists.txt
main/CMakeLists.txt
```

and the repository's existing asset-generation process.

Do not break the existing Assets image generation.

If a LittleFS image is needed, use the LittleFS component's supported CMake integration, for example:

```cmake
littlefs_create_partition_image(
    littlefs
    <source-directory>
    FLASH_IN_PROJECT
)
```

The partition name supplied to the build system must be:

```text
littlefs
```

Do not use:

```text
storage
littlefs
```

for the partition target.

Do not put `main/assets` into the LittleFS image.

---

# 23. OPTIONAL INITIAL LITTLEFS CONTENT

If a build-time LittleFS image is created, use a separate directory such as:

```text
littlefs/
    knx/
```

Do not reuse:

```text
main/assets/
```

The two image-generation paths must remain independent.

---

# 24. PARTITION TABLE VALIDATION

Update all relevant partition tables carefully.

For the standard 16 MB layout, the final table must conceptually contain:

```text
nvs
otadata
phy_init
ota_0
ota_1
assets
littlefs
```

The exact offsets and sizes must be calculated from the actual repository layout.

The final partition must be:

```text
littlefs, data, littlefs, <offset>, <size>
```

The label MUST be:

```text
littlefs
```

Verify:

```text
no overlap
correct alignment
assets image fits
OTA images fit
littlefs fits
```

Use ESP-IDF tooling to validate the generated partition table.

---

# 25. DO NOT BREAK OTHER FLASH CONFIGURATIONS

Inspect all supported flash layouts.

For each configuration determine:

```text
flash size
OTA requirements
Assets size
available LittleFS size
```

If there is insufficient space for a safe LittleFS partition:

- do not break the existing firmware layout;
- do not arbitrarily shrink OTA partitions;
- document the limitation;
- make LittleFS unavailable for that configuration if necessary.

The standard 16 MB configuration should receive the primary LittleFS implementation.

---

# 26. KCONFIG

Only add Kconfig options where useful.

Potential options:

```text
CONFIG_XIAOZHI_LITTLEFS
CONFIG_XIAOZHI_LITTLEFS_MOUNT_POINT
CONFIG_XIAOZHI_LITTLEFS_PARTITION_LABEL
CONFIG_XIAOZHI_FTP_SERVER_ROOT
```

If configurable, the defaults must be:

```text
CONFIG_XIAOZHI_LITTLEFS_PARTITION_LABEL = littlefs
CONFIG_XIAOZHI_LITTLEFS_MOUNT_POINT      = /littlefs
CONFIG_XIAOZHI_FTP_SERVER_ROOT           = /littlefs
```

Do not create unnecessary configuration complexity.

---

# 27. ERROR HANDLING

Handle:

### Partition missing

```text
LittleFS partition littlefs not found
```

Do not crash.

### Mount failure

Do not format automatically.

### Filesystem full

Return a clear error.

### File write failure

Do not report success.

### FTP startup failure

Do not start FTP against a nonexistent filesystem.

### Directory creation failure

Log the error and prevent dependent functionality from falsely reporting success.

---

# 28. LITTLEFS FORMAT

Provide a controlled formatting API if required:

```cpp
bool Format();
```

Formatting must explicitly target:

```text
littlefs
```

and never:

```text
assets
```

Never automatically invoke formatting during normal boot.

---

# 29. RUNTIME TEST

Add or execute a test covering:

```text
Mount LittleFS
    ↓
mkdir /littlefs/interfaces
    ↓
create file
    ↓
write
    ↓
close
    ↓
read
    ↓
verify contents
    ↓
rename
    ↓
delete
```

The test must operate specifically on:

```text
/littlefs
```

and therefore the partition:

```text
littlefs
```

---

# 30. REBOOT PERSISTENCE TEST

Before reboot:

```text
/littlefs/interfaces/knxConfig.json
```

must exist.

After reboot:

```text
Mount partition littlefs
    ↓
/littlefs/interfaces/knxConfig.json
```

must still exist and contain the same data.

This proves that LittleFS is actually using flash storage rather than RAM.

---

# 31. FTP END-TO-END TEST

Perform:

```text
FTP connect
    ↓
LIST /
    ↓
MKD interfaces
    ↓
STOR interfaces/knxConfig.json
    ↓
RETR interfaces/knxConfig.json
    ↓
verify contents
    ↓
reboot ESP32
    ↓
FTP reconnect
    ↓
RETR interfaces/knxConfig.json
```

All operations must work against:

```text
/littlefs
```

---

# 32. ASSETS REGRESSION TEST

After adding LittleFS verify that:

```text
Assets
```

still successfully accesses:

```text
assets/interfaces/knxConfig.json
```

and other existing packaged assets.

LittleFS must not interfere with the Assets partition.

Verify that:

```text
assets
```

and:

```text
littlefs
```

are independently mounted/accessed.

---

# 33. REQUIRED FINAL STORAGE MAP

The final implementation must clearly produce:

```text
ESP32 Flash
│
├── assets partition
│   └── existing XiaoZhi Assets
│       └── interfaces/knxConfig.json
│
└── littlefs partition
    └── LittleFS
        └── /littlefs
            ├── interfaces/
            │   └── knxConfig.json
            └── ...
```

The critical identifiers are:

```text
Partition label: littlefs
Filesystem:      LittleFS
Mount point:     /littlefs
FTP root:        /littlefs
KNX runtime:     /littlefs/interfaces/knxConfig.json
```

---

# 34. REQUIRED FILES TO INSPECT

Before editing, inspect the actual current contents of:

```text
CMakeLists.txt
main/CMakeLists.txt
main/idf_component.yml
main/Kconfig.projbuild

main/assets.cc
main/assets.h

main/settings.cc
main/settings.h

main/application.cc
main/application.h
main/main.cc

main/ftp_server/ftp_server.cc
main/ftp_server/ftp_server.h

main/knx/knx_config.cc
main/knx/knx_config.h
main/knx/knx_manager.cc
main/knx/knx_manager.h
main/knx/knx_mcp_tools.cc

partitions/*
```

Also inspect the existing FTP and KNX documentation.

Do not assume another XiaoZhi fork has the same architecture.

---

# 35. DO NOT PROVIDE GENERIC SAMPLE CODE

This is an actual repository implementation task.

Do not return a generic ESP-IDF LittleFS tutorial.

Implement the changes against:

```text
betamoojw/xiaozhi-esp32
feat/knx_ip
```

Use the actual repository structure and APIs.

---

# 36. ACCEPTANCE CRITERIA

The implementation is complete only when:

### Partition

The generated partition table contains:

```text
assets
littlefs
```

with no overlap.

### Partition label

The LittleFS partition label is exactly:

```text
littlefs
```

### Mount

LittleFS mounts at:

```text
/littlefs
```

from:

```text
littlefs
```

### Assets

The existing `assets` filesystem continues to work.

### FTP

FTP root is:

```text
/littlefs
```

### KNX

Runtime KNX file is:

```text
/littlefs/interfaces/knxConfig.json
```

### Persistence

The file survives reboot.

### Security

FTP cannot escape `/littlefs`.

### Formatting

A failed mount does NOT automatically format the `littlefs` partition.

### Build

The project builds successfully using the repository's normal ESP-IDF build process.

---

# 37. REQUIRED DELIVERABLE

After implementation provide:

## A. Architecture summary

Explain:

```text
assets → existing Assets filesystem
littlefs    → LittleFS
```

## B. Changed files

List every modified and newly-created file.

## C. Final partition table

Provide exact:

```text
name
type
subtype
offset
size
```

for every changed partition.

## D. LittleFS configuration

Explicitly show:

```text
partition label = littlefs
mount point     = /littlefs
```

## E. FTP configuration

Show:

```text
FTP root = /littlefs
```

## F. KNX configuration

Explain:

```text
factory:
assets/interfaces/knxConfig.json

runtime:
 /littlefs/interfaces/knxConfig.json
```

and how this interacts with the existing NVS/KNX persistence mechanism.

## G. Build commands

Provide the exact commands used.

## H. Test results

Report:

```text
build
partition validation
LittleFS mount
file create
file read
file write
file rename
file delete
reboot persistence
FTP LIST
FTP STOR
FTP RETR
FTP path traversal protection
Assets regression
KNX configuration regression
```

## I. Complete patch

Provide the complete production-ready patch/diff for every changed file.

Do not provide pseudo-code.

The implementation must use:

```text
partition label = littlefs
mount point     = /littlefs
```

throughout the complete solution.