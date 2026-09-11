# LittleFS Runtime Storage

XiaoZhi uses two independent flash storage mechanisms:

| Partition | Filesystem | Purpose |
| --- | --- | --- |
| `assets` | Existing packaged Assets/SPIFFS image | Read-only firmware assets |
| `lfs` | LittleFS | Writable runtime and user files mounted at `/littlefs` |

The `Assets` API and its `assets` partition are unchanged. LittleFS is mounted
once during application initialization, before network services start. A mount
failure is logged and disables filesystem-dependent services without formatting
or erasing the partition. Formatting is available only through the explicit
`LittleFsStorage::Format()` maintenance API.

For layouts containing `lfs`, CMake creates and flashes a separate formatted
image from `littlefs/`. It seeds only the `interfaces` directory and never
includes files from `main/assets`. Layouts without `lfs` skip image generation.

The supported V2 flash layouts use these exact ranges:

| Layout | Partition | Type | Subtype | Offset | Size |
| --- | --- | --- | --- | --- | --- |
| 16 MB | `assets` | data | spiffs | `0x800000` | `0x600000` |
| 16 MB | `lfs` | data | littlefs | `0xE00000` | `0x200000` |
| 16 MB C3/C6 | `assets` | data | spiffs | `0x800000` | `0x3E8000` |
| 16 MB C3/C6 | `lfs` | data | littlefs | `0xC00000` | `0x200000` |
| 32 MB | `assets` | data | spiffs | `0xA00000` | `0x1000000` |
| 32 MB | `lfs` | data | littlefs | `0x1A00000` | `0x200000` |

The standard 16 MB Assets partition was reduced from 8 MB to 6 MB after
checking the generated Assets image (1,779,244 bytes). OTA partition sizes and
offsets remain unchanged. Smaller V2 and legacy V1 layouts do not gain an `lfs`
partition; LittleFS reports unavailable on those layouts rather than consuming
space required by OTA or existing assets.

## KNX Configuration

The factory configuration remains the packaged asset
`interfaces/knxConfig.json`. NVS namespace `knx`, key `config` remains the
authoritative runtime persistence mechanism. The canonical runtime file is:

```text
/littlefs/interfaces/knxConfig.json
```

At startup and after direct MCP imports, the selected canonical configuration
is synchronized to that file using write, flush, `fsync`, close, and rename.
To commission through FTP, upload the completed file to:

```text
/littlefs/interfaces/knxConfig.json.tmp
```

Then invoke the owner-only `self.knx.import_configuration_file` MCP tool. It
reads the complete staging file, applies the existing size and JSON validation,
atomically publishes canonical `knxConfig.json`, persists the same value to NVS,
and only then activates the validated registry. Invalid uploads remain inactive.

The LittleFS implementation serializes its filesystem internals with its
FreeRTOS lock. Application-level KNX imports are additionally serialized by
`KnxManager` and do not hold a filesystem lock across FTP network transfers.
LittleFS 1.20.1 does not expose symbolic-link creation through its ESP VFS, and
the FTP protocol has no link command; path confinement therefore normalizes
and bounds lexical paths before every filesystem operation.