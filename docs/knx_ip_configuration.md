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
| `CONFIG_XIAOZHI_KNX_IP_MAX_OBJECTS` | `32` | Maximum logical objects |
| `CONFIG_XIAOZHI_KNX_IP_DEBUG` | off | Log received object values |

The upstream component separately configures callback capacity, packet buffer,
receive-task stack, and receive-task priority under `ESP KNX/IP component`.
The number of unique configured group addresses cannot exceed
`CONFIG_ESP_KNX_IP_MAX_GROUP_ADDRESSES`./

Only three-level group addresses (`main/middle/sub`) and three-level individual
addresses (`area.line.member`) are accepted. Group ranges are 0..31, 0..7, and
0..255. Individual-address ranges are 0..15, 0..15, and 0..255.

## Object Registry

Objects are loaded from NVS namespace `knx`, string key `objects`. The value is
a version-controlled deployment input containing a JSON array. Provision it as
part of device manufacturing or site commissioning; changing it does not
require rebuilding firmware, but the current implementation loads it at boot.

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
  family controls encoding; subtype metadata is retained only in deployment
  documentation in this version.
- `readable` and `writable` must be JSON booleans.
- Multiple objects may share an address only when their DPT family is equal.
- Invalid JSON rejects the complete registry. KNX reports an error while the
  rest of XiaoZhi continues normally.

Use separate status and command objects when the KNX installation has separate
feedback and actuator group addresses. Mark sensor/status objects non-writable.
Do not grant write access merely to make an AI command succeed.

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
- Values stay unknown: verify group responses/feedback are routed and use the
  configured address and DPT.
- Reads do not complete synchronously: this is intentional; request a read and
  then query the cache after the bus response.