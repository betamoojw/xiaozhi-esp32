# Task: Fix KNX configuration storage and persistence in `feat/knx_ip`

## Repository

Repository:

`https://github.com/betamoojw/xiaozhi-esp32`

Target branch:

`feat/knx_ip`

## Objective

Fix the KNX configuration error:

```text
KNX configuration directory is unavailable at assets/interfaces;
using default KNX configuration
```

The root cause is that the current KNX implementation incorrectly treats:

```text
/littlefs/interfaces/knxConfig.json
```

as a normal writable POSIX filesystem file.

In this project, `assets` is a packed/read-only asset partition accessed through the XiaoZhi `Assets` API. It is not a normal writable filesystem directory.

The implementation must therefore separate:

1. **Factory/default configuration**
   - `main//littlefs/interfaces/knxConfig.json`
   - packaged into the read-only Assets image
   - accessed using `Assets::GetAssetData()`

2. **Runtime configuration**
   - persisted in NVS
   - namespace: `knx`
   - key: `config`
   - loaded with priority over the factory configuration

Do NOT attempt to make `assets/interfaces` writable and do NOT solve the problem by creating a filesystem directory.

---

# Required runtime behavior

Implement this configuration loading priority:

```text
Boot
 │
 ▼
NVS: namespace "knx", key "config"
 │
 ├── configuration exists
 │       │
 │       ▼
 │   validate/parse
 │       │
 │       ▼
 │   use runtime configuration
 │
 └── configuration does not exist
         │
         ▼
 Assets::GetAssetData("interfaces/knxConfig.json")
         │
         ├── success
         │     ▼
         │  validate/parse
         │
         └── failure
                ▼
          existing built-in emergency
          kValidConfiguration
```

For runtime import:

```text
self.knx.import_configuration
        │
        ▼
validate complete JSON
        │
        ├── invalid
        │      └── reject without changing
        │          active or persisted config
        │
        └── valid
               │
               ▼
        persist canonical JSON to NVS
               │
               ├── NVS failure
               │      └── reject without changing
               │          active configuration
               │
               └── success
                      │
                      ▼
               replace active registry
                      │
                      ▼
               restart/rebind KNX routing
```

The imported configuration must survive a device reboot.

---

# Files to inspect first

Before editing, inspect the actual current contents of:

```text
main/knx/knx_config.h
main/knx/knx_config.cc
main/knx/knx_manager.cc
main/knx/knx_manager.h
main/knx/knx_mcp_tools.cc
main/assets.h
main/assets.cc
main/settings.h
main/settings.cc
docs/knx_ip_configuration.md
```

Also inspect the project's asset-generation/packaging mechanism.

Do not assume the previous implementation details are unchanged. Base the final patch on the actual current `feat/knx_ip` branch.

---

# Required changes

## 1. Remove the writable filesystem assumption

In:

```text
main/knx/knx_config.h
main/knx/knx_config.cc
main/knx/knx_manager.cc
```

remove the use of:

```cpp
stat("assets/interfaces", ...)
fopen("/littlefs/interfaces/knxConfig.json", ...)
fwrite(...)
fflush(...)
rename(...)
remove(...)
```

for KNX configuration.

Do not use:

```cpp
kKnxConfigurationDirectory
kKnxConfigurationPath
```

as writable filesystem paths.

The warning:

```text
KNX configuration directory is unavailable
```

must disappear because the KNX manager should no longer check for such a directory.

---

# 2. Use the Assets API for the factory configuration

Define a constant similar to:

```cpp
constexpr char kKnxFactoryConfigurationAsset[] =
    "interfaces/knxConfig.json";
```

Use the project's actual `Assets` API to read this asset.

Conceptually:

```cpp
void* data = nullptr;
size_t size = 0;

if (!Assets::GetInstance().GetAssetData(
        kKnxFactoryConfigurationAsset,
        data,
        size)) {
    ...
}
```

Use the exact API/signature present in the current repository.

Do not invent a new Assets API.

Validate:

```text
size > 0
size <= kKnxMaximumConfigurationLength
```

before parsing.

Do not copy the asset into a filesystem.

---

# 3. Use the existing Settings/NVS infrastructure

Inspect:

```text
main/settings.h
main/settings.cc
```

Use the existing `Settings` abstraction rather than implementing a second independent NVS layer unless the existing abstraction cannot support this requirement.

Use:

```text
namespace: knx
key:       config
```

For example:

```cpp
constexpr char kKnxSettingsNamespace[] = "knx";
constexpr char kKnxSettingsKey[] = "config";
```

Use the existing project's API equivalent to:

```cpp
Settings settings("knx", true);
settings.SetStringAndCommit("config", canonical_json);
```

and:

```cpp
Settings settings("knx");
settings.GetString("config", "");
```

Use the exact signatures/types in the repository.

Check and handle all NVS errors.

---

# 4. Add clear configuration storage functions

Refactor `knx_config.h/.cc` so the storage layer exposes separate operations similar to:

```cpp
bool KnxLoadFactoryConfiguration(
    std::string& json_text,
    std::string& error);

bool KnxLoadPersistedConfiguration(
    std::string& json_text,
    bool& found,
    std::string& error);

bool KnxPersistConfiguration(
    const std::string& json_text,
    std::string& error);
```

Use appropriate names if the existing code has a better naming convention.

Important:

- Factory configuration = Assets
- Runtime configuration = NVS
- Never write to Assets
- Never use filesystem APIs for KNX configuration

---

# 5. Preserve the existing JSON parser and validation

Do NOT weaken or bypass the existing:

```text
KnxParseConfiguration()
```

validation.

Preserve all existing validation behavior, including:

- JSON validity
- required fields
- field types
- object ID validation
- duplicate object ID detection
- group-address validation
- duplicate group-address detection
- DPT validation
- readable/writable validation
- object-count limits
- maximum configuration size
- canonical JSON generation
- unknown-field handling

Do not duplicate the parser.

The parser should remain the single source of truth.

---

# 6. Change `KnxManager::LoadConfiguration()`

Change boot loading logic to:

```text
1. Try NVS.
2. If NVS contains a configuration:
      use it.
3. If NVS does not contain a configuration:
      load `interfaces/knxConfig.json`
      through Assets.
4. If the factory asset cannot be loaded:
      use the existing `kValidConfiguration`.
5. Parse and validate the selected JSON.
6. Populate the active KNX object registry.
```

Do not treat an empty NVS value as automatically equivalent to "not found" unless that matches the existing configuration semantics.

In particular, the valid configuration:

```json
[]
```

must be distinguishable from "no persisted configuration".

If NVS contains `"[]"`, that must be treated as a valid persisted empty registry.

---

# 7. Change `KnxManager::ImportConfiguration()`

The current implementation attempts to write:

```text
/littlefs/interfaces/knxConfig.json
```

Replace that behavior.

Required order:

```text
1. Parse/validate candidate JSON.
2. Generate canonical JSON.
3. Persist canonical JSON to NVS.
4. Only after NVS persistence succeeds:
      replace the active `objects_`.
5. Update configuration state.
6. Trigger the existing immediate KNX restart/rebind mechanism.
```

This ordering is important.

If parsing fails:

```text
active configuration unchanged
NVS unchanged
KNX unchanged
```

If NVS persistence fails:

```text
active configuration unchanged
NVS unchanged/previous value retained
KNX unchanged
```

Only a successful validation + successful NVS commit should modify the active registry.

---

# 8. Preserve immediate application

The existing MCP behavior is intended to apply imported configuration without reboot.

Preserve the existing mechanism involving:

```text
force_restart_
state_
lifecycle_task_
network availability
KNX routing restart
```

Do not unnecessarily redesign the KNX lifecycle.

After a successful import:

```text
MCP import
    ↓
NVS commit
    ↓
active registry replaced
    ↓
existing KNX restart/rebind mechanism
```

No reboot should be required.

---

# 9. Preserve the built-in emergency configuration

Do not remove:

```cpp
kValidConfiguration
```

unless the current repository has a better established fallback.

It should remain the final fallback:

```text
NVS
 ↓
factory Assets
 ↓
kValidConfiguration
```

The fallback should only be used if neither persisted nor factory configuration is available/valid according to the intended error semantics.

Do not silently discard an invalid persisted configuration and unexpectedly replace it with a factory configuration without logging the reason.

Prefer clear diagnostic logs.

---

# 10. Ensure `knxConfig.json` is actually packaged

Inspect the repository's asset packaging/build scripts.

Confirm that:

```text
main//littlefs/interfaces/knxConfig.json
```

is included in the generated Assets image with the runtime asset name:

```text
interfaces/knxConfig.json
```

If the current asset-generation mechanism does not include it, modify the appropriate asset manifest/build configuration.

Do NOT modify `Assets` into a writable filesystem just to solve this.

The required runtime lookup is:

```cpp
Assets::GetAssetData(
    "interfaces/knxConfig.json",
    ...);
```

---

# 11. Update documentation

Update:

```text
docs/knx_ip_configuration.md
```

so it no longer claims that:

```text
assets/interfaces
```

must be a writable runtime directory.

Document:

```text
main//littlefs/interfaces/knxConfig.json
```

as the factory/default configuration.

Document:

```text
NVS namespace = knx
NVS key       = config
```

as the runtime persistent configuration.

Document that:

```text
self.knx.import_configuration
```

validates and persists the runtime configuration to NVS and applies it immediately.

Update troubleshooting documentation so it no longer tells users to fix:

```text
KNX configuration directory is unavailable
```

by creating a directory.

---

# 12. Update MCP descriptions if necessary

Inspect:

```text
main/knx/knx_mcp_tools.cc
```

If the tool description currently says that it replaces:

```text
/littlefs/interfaces/knxConfig.json
```

or otherwise implies that the file is writable at runtime, update the description.

Use wording equivalent to:

```text
Imports and persists the complete KNX communication-object registry.
The configuration is validated before replacing the active registry
and persistent KNX configuration.
```

Preserve the existing MCP API and JSON request/response format unless a change is absolutely necessary.

---

# 13. Do not introduce unnecessary architecture changes

Do NOT:

- replace the existing Assets system
- add a new filesystem
- remount the assets partition
- create `assets/interfaces` at runtime
- add another NVS abstraction if `Settings` already works
- rewrite `KnxParseConfiguration()`
- redesign KNX/IP networking
- modify unrelated Modbus functionality
- modify unrelated XiaoZhi functionality
- change the MCP protocol unnecessarily

Keep the patch focused on KNX configuration storage/persistence.

---

# 14. Logging requirements

Use clear logs.

Expected examples:

```text
Loading KNX configuration from NVS
```

or:

```text
Loading factory KNX configuration from assets
```

or:

```text
Factory KNX configuration asset unavailable; using emergency built-in configuration
```

For persistence:

```text
Persisted KNX configuration to NVS
```

For failures:

```text
Could not persist KNX configuration to NVS: <error>
```

Do NOT emit:

```text
KNX configuration directory is unavailable
```

because there should no longer be a directory check.

---

# 15. Test cases

After implementing the patch, test all of the following.

## Test A — Factory configuration

With no existing NVS KNX configuration:

```text
NVS knx/config = not found
```

Verify:

```text
Assets::GetAssetData("interfaces/knxConfig.json")
```

loads the factory configuration.

Verify the expected objects become active.

---

## Test B — No filesystem warning

Boot the device and verify this message does NOT appear:

```text
KNX configuration directory is unavailable
```

---

## Test C — Runtime import

Call:

```text
self.knx.import_configuration
```

with a valid configuration.

Verify:

```text
imported = true
persisted = true
```

and the expected object count.

---

## Test D — Immediate application

Immediately call:

```text
self.knx.list_objects
```

without rebooting.

Verify that the newly imported registry is active.

---

## Test E — Persistence after reboot

After Test C:

1. reboot ESP32
2. call:

```text
self.knx.list_objects
```

Verify that the imported configuration is loaded from NVS rather than the factory asset.

---

## Test F — Empty registry

Import:

```json
[]
```

Verify:

```text
imported = true
persisted = true
object_count = 0
```

Reboot.

Verify the active registry remains empty.

This confirms that `"[]"` is not incorrectly treated as "configuration not found".

---

## Test G — Invalid configuration

Attempt to import an invalid configuration.

Verify:

```text
imported = false
persisted = false
```

and:

```text
previous active configuration remains unchanged
previous persisted configuration remains unchanged
KNX routing is not unnecessarily restarted
```

---

## Test H — NVS failure

If practical, simulate or force an NVS persistence failure.

Verify that:

```text
active registry is not replaced
```

when persistence fails.

---

# 16. Build verification

Build the target using the repository's normal ESP-IDF build process.

Fix any compile errors caused by the patch.

Pay particular attention to:

- `Assets` API signatures
- `Settings` constructor signatures
- `Settings::GetString()` return type
- `Settings::SetStringAndCommit()` return type
- required includes
- ESP-IDF error types
- C++ standard supported by this project

Do not assume APIs from another XiaoZhi version.

Use the actual APIs in this branch.

---

# 17. Final response requirements

After making the changes, provide:

1. A concise root-cause summary.
2. List of modified files.
3. Explanation of the new configuration flow.
4. The exact NVS namespace/key used.
5. Any asset packaging changes made.
6. Build result.
7. Test results.
8. Any remaining issue or uncertainty.

If a requested test cannot be executed because hardware is unavailable, clearly state that and provide the exact command/procedure that should be run on hardware.

Do not claim a hardware test passed if it was not actually performed.

---

# Success criteria

The task is complete only when:

```text
main//littlefs/interfaces/knxConfig.json
```

is treated as a read-only factory asset,

and:

```text
NVS:
    namespace = "knx"
    key       = "config"
```

is the runtime persistent configuration.

The following must no longer occur:

```text
stat("assets/interfaces")
fopen("/littlefs/interfaces/knxConfig.json")
rename(..., "/littlefs/interfaces/knxConfig.json")
KNX configuration directory is unavailable
```

A valid configuration imported through:

```text
self.knx.import_configuration
```

must:

```text
validate
→ persist to NVS
→ update active registry
→ restart/rebind KNX
→ survive reboot
```

while:

```text
main//littlefs/interfaces/knxConfig.json
```

remains the factory default fallback.