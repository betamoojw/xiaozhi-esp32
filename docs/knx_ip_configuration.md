# KNX/IP Configuration

## Kconfig

Open `Xiaozhi Assistant -> KNX/IP Configuration` in menuconfig.

| Option | Default | Purpose |
| --- | --- | --- |
| `CONFIG_XIAOZHI_KNX_IP` | off | Compile and enable the adapter |
| `CONFIG_XIAOZHI_KNX_IP_PHYSICAL_ADDRESS` | `15.15.199` | KNX individual source address |
| `CONFIG_XIAOZHI_KNX_IP_MULTICAST_ADDRESS` | `224.0.23.12` | Routing multicast group |
| `CONFIG_XIAOZHI_KNX_IP_PORT` | `3671` | Routing UDP port |
| `CONFIG_XIAOZHI_KNX_IP_RECONNECT_INTERVAL_MS` | `10000` | Start/restart retry interval |
| `CONFIG_XIAOZHI_KNX_IP_MAX_OBJECTS` | `128` | Maximum logical objects |
| `CONFIG_XIAOZHI_KNX_IP_DEBUG` | off | Log received object values |

The upstream component separately configures callback capacity, packet buffer,
receive-task stack, and receive-task priority under `ESP KNX/IP component`.
The number of unique configured group addresses cannot exceed
`CONFIG_ESP_KNX_IP_MAX_GROUP_ADDRESSES`.

Only three-level group addresses (`main/middle/sub`) and three-level individual
addresses (`area.line.member`) are accepted. Group ranges are 0..31, 0..7, and
0..255. Individual-address ranges are 0..15, 0..15, and 0..255.

## Object Registry

The canonical registry is `/littlefs/interfaces/knxConfig.json` in the writable
LittleFS partition, with a maximum serialized size of 65,535 bytes. At boot, the
firmware loads and validates that file. If it does not exist, an existing value
from the legacy NVS namespace `knx`, key `config`, is validated and atomically
migrated to LittleFS. If neither source exists, the firmware writes and loads
an empty registry (`[]`). The legacy NVS value is retained as a non-destructive
rollback copy but is not read while the LittleFS file exists and is not updated
by new imports.

An invalid or unreadable LittleFS registry is never replaced automatically;
KNX enters the error state and reports the validation or filesystem error. A
LittleFS mount failure likewise disables KNX configuration loading without
formatting or erasing the partition.

Provision runtime configuration through the owner-only MCP tool
`self.knx.import_configuration`. The tool validates the complete candidate,
atomically persists canonical JSON to LittleFS, updates the in-memory registry
only after the commit succeeds, and restarts KNX routing to bind callbacks to the new group
addresses. No device reboot is required to apply a successful import.

Each entry requires:

```json
{
  "id": "site_unique_id",
  "name": "Human-readable semantic name",
  "description": "Optional concise location and function",
  "group_address": "1/2/3",
  "datapoint_type": "DPT-1.001",
  "readable": true,
  "writable": false
}
```

This is a schema example, not a built-in object. The firmware contains no
site-specific group addresses.

Rules:

- `id` is required, unique, and at most 48 bytes.
- `name` is required and at most 80 bytes.
- `description` is optional and at most 192 bytes.
- `group_address` must use exact three-level syntax.
- `datapoint_type` accepts a supported family with an optional subtype. The
  exact subtype is retained. DPT 4.001 uses ASCII conversion, DPT 5.001 uses
  percent scaling, and DPT 5.003 uses angle conversion; other subtypes use the
  codec for their main family.
- `readable` and `writable` must be JSON booleans.
- At least one of `readable` or `writable` must be true.
- Object IDs and group addresses must both be unique.
- Unknown or duplicate JSON fields are rejected.
- Invalid JSON rejects the complete registry. KNX reports an error while the
  previous registry and persisted configuration remain unchanged.

Supported main families match the managed component exactly: DPT 1 through
DPT 31, DPT 232 (RGB), DPT 234 (language), and DPT 251 (RGBW). The component
does not provide a subtype catalogue, so configuration validates the family
and preserves the subtype rather than claiming semantic validation of every
KNX subtype number.

Use separate status and command objects when the KNX installation has separate
feedback and actuator group addresses. Mark sensor/status objects non-writable.
Do not grant write access merely to make an AI command succeed.

## Import Procedure

1. Start with [the generic test registry](../main/knx/test/knx_objects.test.json)
  or create a site-specific JSON array using the schema above.
2. Open an authenticated MCP client that can list tools with the `user`
  audience.
3. Call `self.knx.import_configuration` with the complete JSON array encoded
  as the string property `configuration`.
4. Confirm the response contains `imported: true`, `persisted: true`, and the
  expected `object_count`.
5. Call `self.knx.list_objects` and `self.knx.get_status` to verify the active
  registry and routing state.
6. Reboot the device and list the objects again to verify LittleFS persistence.

Example MCP arguments using the test switch command:

```json
{
  "configuration": "[{\"id\":\"test_switch_command\",\"name\":\"Test Switch Command\",\"description\":\"Generic commissioning object\",\"group_address\":\"1/0/1\",\"datapoint_type\":\"DPT-1.001\",\"readable\":false,\"writable\":true}]"
}
```

Import `[]` to persist an empty registry and remove all configured callbacks.
The import tool is deliberately user-only so the AI model cannot rewrite its
own KNX authorization boundary.

## Cache Semantics

Every object starts with `valid = false`. Incoming KNX write or response
telegrams set the decoded value, `valid = true`, and a monotonic millisecond
timestamp. Outgoing writes do not optimistically update the cache. This avoids
reporting an actuator state that the bus never confirmed.

Cached values survive a network disconnect in RAM with their original
timestamp. Clients must consider the routing state and timestamp before treating
them as current. Cache values are not persisted across reboot.

## Troubleshooting

- `waiting_for_network`: the board has not reported a usable connection.
- `Active network does not expose an ESP-IDF netif`: the selected transport is
  not a supported common Wi-Fi/Ethernet path.
- `KNX start failed`: inspect bind, multicast membership, IP, and port use.
- `Invalid KNX communication object configuration`: validate every required
  field, address, DPT, boolean, length, and duplicate.
- `LittleFS is not mounted`: verify the selected partition table contains the
  `littlefs` partition and inspect the earlier mount error.
- `Could not open/read KNX runtime configuration`: inspect LittleFS health and
  file permissions. Invalid or unreadable data is not silently replaced.
- `Could not load legacy KNX configuration from NVS`: NVS is consulted only
  when the runtime file is absent; inspect NVS initialization and partition health.
- `Could not initialize KNX runtime configuration`: migration or empty-registry
  initialization could not be committed to LittleFS, so KNX remains disabled.
- Values stay unknown: verify group responses/feedback are routed and use the
  configured address and DPT.
- Reads do not complete synchronously: this is intentional; request a read and
  then query the cache after the bus response.