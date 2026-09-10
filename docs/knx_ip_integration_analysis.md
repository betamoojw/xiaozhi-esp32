# KNX/IP Integration Analysis

## Scope and Baseline

This analysis covers XiaoZhi commit `c7241272f2d5fd140c77542f3cf12d09e717fc2f`
and `betamoojw/esp32_knx_ip` commit
`781faca714948c4bc081f3319a5eddbcccd5be9f`. The local branch is
`feat/knx_ip`. XiaoZhi declares ESP-IDF `>=5.5.2`; the project policy prefers
ESP-IDF 6.0.2 while retaining 5.5.2 for documented legacy boards. The current
shell did not have an ESP-IDF environment loaded, so build compatibility still
requires executable validation under both supported SDK lines.

The integration is limited to the upstream component's implemented transport:
KNXnet/IP routing over IPv4 UDP. Tunneling and KNX Secure are not implemented
and must not be exposed as supported modes.

## XiaoZhi Architecture Relevant to KNX

- `main/main.cc` initializes NVS before constructing and initializing the
  application.
- `Application::Initialize()` initializes UI and audio, registers common MCP
  tools, installs the board's single network event callback, and then starts
  networking asynchronously.
- Board implementations report transport-neutral `NetworkEvent::Connected`
  and `NetworkEvent::Disconnected` events. `Application` converts those events
  to event-group bits and handles them on its main task.
- `Board::GetNetwork()` returns XiaoZhi's generic `NetworkInterface`. It does
  not expose the native `esp_netif_t*` required for multicast membership.
  Wi-Fi and Ethernet boards own suitable native interfaces, but cellular and
  some externally hosted network implementations may not.
- Core code depends on `Board`, never a concrete board. Any native-netif access
  must therefore be an optional virtual capability on `Board`, implemented by
  reusable Wi-Fi/Ethernet board bases and unavailable by default.
- `McpServer::AddTool()` is the existing device tool mechanism. Tool callbacks
  execute through the application scheduler on the main task. They support
  boolean, integer, and string input properties, but no floating-point property
  type. `cJSON*` return values transfer ownership to MCP and are deleted after
  serialization.
- MCP tool-definition pagination is bounded near 8 KB, but tool result payloads
  require their own limits. KNX object listing therefore needs `offset` and
  `limit` parameters with a conservative maximum.
- `Settings` is a scalar NVS wrapper. A versioned, bounded JSON string in a KNX
  namespace can hold a modest dynamic object registry without introducing a
  filesystem dependency. Invalid persisted data must leave KNX disabled while
  the rest of XiaoZhi continues normally.
- `main/CMakeLists.txt` owns core source and include registration. Optional KNX
  sources should only be appended when `CONFIG_XIAOZHI_KNX_IP` is enabled.
- `main/Kconfig.projbuild` is the correct place for user-facing XiaoZhi KNX
  options. Low-level task and packet sizes remain in the component's Kconfig.

## KNX/IP Component Architecture

The managed component is named `esp_knx_ip` and lives at
`components/esp_knx_ip` in the upstream repository. Its manifest version is
1.0.0 and declares `idf >=5.0`. It publicly requires `esp_netif`, privately
requires `nvs_flash`, and compiles as C++17. The optional
`esp_knx_ip_web` component is not needed and must not be added.

Public API:

- Lifecycle: `esp_knx_ip_create`, `esp_knx_ip_start`, `esp_knx_ip_stop`,
  `esp_knx_ip_destroy`, and `esp_knx_ip_get_state`.
- Address configuration: `esp_knx_ip_set_physical_address` and
  `esp_knx_ip_get_physical_address`.
- Receive registration: `esp_knx_ip_register_callback`,
  `esp_knx_ip_unregister_callback`, and optional persistent mapping APIs.
- Transmission: `esp_knx_ip_send` and `esp_knx_ip_send_unicast`.
- Address helpers: `knx_group_address`, `knx_physical_address`, and component
  extraction helpers.
- DPT codecs: DPT 1, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 16, and 232.

The component owns one UDP socket, mutex, event group, fixed callback table,
and FreeRTOS receive task per handle. It binds UDP port 3671, joins multicast
group `224.0.23.12` on the IPv4 address of the supplied `esp_netif_t`, and uses
a multicast TTL of one. The application owns NVS initialization, the network
interface, DHCP, link/IP lifecycle, and reconnect policy.

Callbacks execute on the component's `knx_ip_rx` task. Telegram storage is only
valid for the callback duration. A callback must not call stop or destroy and
must remain short. `esp_knx_ip_stop()` wakes the socket and waits for the
receive task to exit, so lifecycle work must not run in a receive callback and
should not block XiaoZhi's main or audio tasks.

The component implements KNXnet/IP routing indications (`L_Data.ind`) only.
Its unicast function sends the same routing-indication frame to a specific IPv4
endpoint; it is not tunneling. There is no gateway discovery, connection
session, routing busy/lost handling, IPv6 routing, tunneling, authentication,
encryption, or KNX Secure.

### Telegram and DPT Payload Contract

`knx_telegram_t` contains command, source and destination 16-bit addresses,
and up to 255 bytes of APDU data. The first APDU byte retains only its lower six
data bits after the parser removes APCI command bits. DPT 1 uses that first byte.
Multi-byte DPT payloads begin at `telegram.data + 1`; sends must prepend a zero
APDU byte before the encoded DPT bytes.

The XiaoZhi type layer will initially support only codecs present upstream:

| DPT family | XiaoZhi value type | Notes |
| --- | --- | --- |
| DPT 1.x | `bool` | One-bit boolean |
| DPT 5.x | `uint8_t` | Raw 0..255; subtype scaling is not inferred |
| DPT 7.x | `uint16_t` | Raw unsigned value; units depend on configured subtype |
| DPT 9.x | `float` | KNX two-byte float; finite upstream encoder range |
| DPT 12.x | `uint32_t` | Four-byte unsigned integer |
| DPT 13.x | `int32_t` | Four-byte signed integer |
| DPT 14.x | `float` | Four-byte IEEE-754 float; finite values only |

DPT 17.x scenes and DPT 20.x enums are required for evaluation but have no
upstream codecs and will remain explicitly unsupported. DPT 6, 8, 10, 11, 16,
and 232 exist upstream but are outside the initial MCP model and will not be
advertised until their value schemas and subtype validation are designed.

## API and ESP-IDF Compatibility

The component uses stable ESP-IDF APIs: `esp_netif_get_ip_info`, NVS, FreeRTOS,
and lwIP BSD sockets. No obvious source-level incompatibility with IDF 5.5.2 or
6.0.2 was found. This is an inspection result, not a successful build claim.
Component resolution, compile, link, multicast socket behavior, and all target
toolchains still need validation.

The current XiaoZhi manifest tracks upstream `main`, which is not reproducible.
It should use the actual component name and path with an immutable Git revision:

```yaml
esp_knx_ip:
  git: https://github.com/betamoojw/esp32_knx_ip.git
  path: components/esp_knx_ip
  version: 781faca714948c4bc081f3319a5eddbcccd5be9f
  rules:
    - if: xiaozhi_knx_ip == true
```

The exact Component Manager rule syntax must be verified during reconfigure.
If project Kconfig symbols are unavailable to manifest rules at resolution
time, keep the pinned dependency unconditional and conditionally compile only
the XiaoZhi adapter. No component source should be copied into this repository.

The upstream repository contains no `LICENSE` file or SPDX declaration. This is
a production distribution blocker until the upstream owner publishes explicit
license terms. Technical integration may proceed on this branch, but release
must not proceed on an assumed license.

## Network Integration

Add an optional `Board::GetEspNetif()` capability that returns `nullptr` by
default. Implement it in common Wi-Fi and Ethernet board bases using their
existing interface ownership. Dual-network boards return the native interface
only when their active transport provides one. Cellular-only, USB/RNDIS, and
remote-hosted paths remain unsupported unless their native interface semantics
are verified. No second Wi-Fi manager, DHCP client, or event loop is created.

`Application` will notify `KnxManager` after processing connected and
disconnected events. The manager owns a small lifecycle worker task or bounded
queue:

1. On network connected, capture the currently active non-null netif and queue
   start/restart.
2. Create the component handle once a valid IPv4 address exists, register one
   callback per unique configured group address, and start routing.
3. On network disconnected, queue stop. Do not stop from the board callback or
   KNX receive callback.
4. On reconnect or changed netif/IP, stop and restart so multicast membership
   is joined on the current address.
5. Retry failed starts with a bounded Kconfig reconnect interval. A missing
   router is not a boot failure because routing has no gateway session.

`IsConnected()` would be misleading for connectionless routing. Public status
should distinguish `disabled`, `waiting_for_network`, `running`, and `error`.
`running` means the socket is bound and multicast membership was joined, not
that a KNX router or bus was proven reachable.

## Communication Object and Cache Architecture

`KnxCommunicationObject` contains:

- `id`, `name`, and `description`
- parsed group address and canonical `main/middle/sub` string
- DPT family and optional subtype text
- `readable` and `writable`
- type-safe `KnxValue`
- `valid` and monotonic `last_update_ms`

Use `std::variant<bool, uint8_t, uint16_t, uint32_t, int32_t, float>` for the
initial `KnxValue`. Parsing validates exact group-address syntax and ranges
(main 0..31, middle 0..7, sub 0..255), supported DPT, ID length, duplicate ID,
and duplicate group-address policy. Multiple logical objects on one group
address are only accepted when their DPT families agree; one component callback
is registered per unique address and updates every matching logical object.

The registry and state cache are protected by a manager mutex. The receive
callback decodes into a local value, then briefly locks to update value,
validity, timestamp, last communication time, and error state. It performs no
network operation and does not call application/UI/MCP code while locked.
Writes encode outside the lock from a copied object descriptor, send without
holding the registry mutex, and only update the cache upon a received write or
response telegram. Failed sends never fabricate state.

Persist a bounded JSON object array in `assets/interfaces/knxConfig.json`. The maximum
object count is constrained by both Kconfig and the component's
`ESP_KNX_IP_MAX_GROUP_ADDRESSES`. Missing or invalid configuration leaves KNX
in its error state without affecting the rest of XiaoZhi. Configuration can be
provisioned through FTP or the owner-only import tool; both paths use the same
manager validation rules.

## Read and Write Semantics

The component can send `KNX_COMMAND_READ` and receive responses but has no
request correlation, timeout, or transaction API. MCP runs on XiaoZhi's main
task, so `self.knx.read` must not wait synchronously. It will validate readable
permission, send a read telegram, and return a result indicating that the read
was requested plus the existing cached value/validity. A later response updates
the cache asynchronously. `self.knx.get_object` is the deterministic cache
query. A future request API may add a bounded pending-read table and completion
notifications without blocking MCP.

Raw group-address writes are permitted only when the address resolves to a
configured writable object. They use that object's configured DPT; MCP never
chooses an arbitrary encoder or raw APDU. `self.knx.set_object` is the preferred
semantic write API. Floating-point MCP inputs use validated decimal strings
because XiaoZhi's property system currently truncates JSON numbers to integers.
Boolean and integer families use native MCP boolean/integer inputs where the
tool schema permits; a uniform string `value` is acceptable only if parsing is
strictly type-directed and documented.

## MCP Tools

Register these common tools only when KNX is enabled:

- `self.knx.get_status`: routing state, interface availability, endpoint,
  physical address, object/valid counts, last communication time, and last
  error. It must describe routing availability rather than claim a gateway
  connection.
- `self.knx.list_objects`: concise object summaries with bounded `offset` and
  `limit`; returns `next_offset` when more objects exist.
- `self.knx.get_object`: semantic lookup by object ID, including explicit
  `valid: false` and no fabricated value.
- `self.knx.read`: validates a configured readable address, sends an
  asynchronous group read, and reports cached state plus request status.
- `self.knx.write`: validates that a configured object at the address is
  writable and encodes its configured DPT.
- `self.knx.set_object`: preferred semantic write by object ID.

Descriptions will tell the model to list or query objects before guessing IDs,
use get operations for questions, use set operations for control, and treat an
invalid cache entry as unknown. Responses are bounded JSON objects created by
the MCP callback and transferred to MCP ownership.

## Kconfig and SDK Configuration

User-facing options:

- `XIAOZHI_KNX_IP`: enable the integration, default off.
- `XIAOZHI_KNX_IP_PHYSICAL_ADDRESS`: canonical `area.line.member` string.
- `XIAOZHI_KNX_IP_MULTICAST_ADDRESS`: default `224.0.23.12`.
- `XIAOZHI_KNX_IP_PORT`: default 3671.
- `XIAOZHI_KNX_IP_RECONNECT_INTERVAL_MS`: bounded retry delay.
- `XIAOZHI_KNX_IP_MAX_OBJECTS`: bounded logical object count no greater than
  the configured callback capacity unless addresses are shared.
- `XIAOZHI_KNX_IP_DEBUG`: optional adapter log verbosity.

Do not expose tunneling or routing choices: only routing exists. The upstream
component settings `ESP_KNX_IP_MAX_GROUP_ADDRESSES`, packet buffer, task stack,
and task priority remain available as advanced component settings. No global
`sdkconfig.defaults` change is required because KNX defaults off. Board release
variants that enable KNX may set the XiaoZhi symbols and callback capacity in
their own defaults later.

## Concurrency, Memory, and Performance

The upstream handle allocates a context, mutex, event group, socket, callback
table, and a receive task with a default 4096-byte stack. Each receive/send path
uses a default 512-byte stack packet buffer. XiaoZhi adds a bounded worker queue,
worker stack, object strings/variants, and NVS JSON parse allocation. Object
configuration is loaded once and parse trees are deleted immediately.

No unbounded queue is allowed. Network calls occur without the registry mutex.
Callbacks copy/decode before returning. MCP list output is paginated. Start,
stop, and retry run below audio/main priorities. The integration must measure
free heap and minimum task stack watermark on representative non-PSRAM targets.

## Failure Handling

- Missing native netif or zero IPv4 address: remain waiting; do not fail boot.
- Socket/bind/multicast failure: record the ESP error and retry in background.
- Network loss: stop and invalidate routing availability; retain cached values
  with their timestamps rather than marking stale data current.
- Malformed address, unsupported DPT, invalid/range-overflow value, duplicate
  incompatible address, or malformed JSON: reject configuration or MCP call
  with a precise error.
- Callback table exhaustion: reject excess unique addresses during load.
- Write/read send failure: return failure and preserve cache validity/value.
- Component initialization failure: expose error status while XiaoZhi audio,
  UI, protocols, and MCP continue.

## Risks and Mitigations

1. Routing-only support excludes tunneling-only installations. Document and
   verify that the site has a KNX/IP router with multicast routing enabled.
2. KNX Secure is absent. Restrict deployment to trusted LANs and document that
   telegrams are unauthenticated and unencrypted.
3. Upstream licensing is unspecified. Obtain an explicit upstream license
   before release.
4. Active native netif is not exposed today. Add a narrow optional board
   capability and validate Wi-Fi and Ethernet independently.
5. Upstream callback capacity defaults to 32. Validate object configuration
   against unique addresses and avoid silently dropping registrations.
6. Group reads have no transaction layer. Keep the MCP operation asynchronous
   and cache-based; never block XiaoZhi's main task.
7. DPT subtype semantics are incomplete. Support only exact implemented
   families and do not infer percentages, units, scenes, or HVAC enums.
8. Tracking upstream `main` is not reproducible. Pin the reviewed commit.
9. Port 3671 may already be in use. Report bind failure and leave XiaoZhi
   operational.
10. Stale cache can be mistaken for live state. Always return validity,
    timestamp, and routing state together.

## Testing Strategy

### Host and Static Tests

- Run upstream host protocol/DPT tests at the pinned revision.
- Add host-testable XiaoZhi tests for group/physical address parsing,
  canonical formatting, all supported DPT round trips and bounds, object JSON
  validation, duplicate handling, cache validity, and pagination.
- Check C/C++ formatting only for touched files.

### ESP-IDF Build Tests

- Resolve dependencies from a clean managed-components state under ESP-IDF
  6.0.2 and legacy 5.5.2 where supported.
- Build an ESP32-S3 Wi-Fi variant with KNX disabled and enabled.
- Build a representative Ethernet-capable variant with KNX enabled.
- Build at least one constrained ESP32/C3 path with KNX disabled to prove no
  regression from the optional integration.
- Run existing `python3 -m unittest discover -s scripts/tests -v` tests.

### Hardware and Network Tests

- Verify multicast join, routing read/write/response telegrams, incoming write
  cache updates, malformed packet rejection, and router interoperability.
- Disconnect/reconnect Wi-Fi, renew DHCP, and repeat with Ethernet; confirm the
  old membership is stopped and the new address rejoins.
- Exercise unavailable router, socket failure, callback-capacity exhaustion,
  repeated start/stop, and reboot with valid/invalid persisted configuration.
- Verify audio capture/playback, wake/VAD, UI responsiveness, MCP calls, and
  protocol reconnect while KNX traffic is active.
- Measure heap, receive/worker task stack watermarks, and MCP response sizes.

A successful build is not KNX bus or hardware validation.

## Recommended Implementation

Add:

- `main/knx/knx_object.h`
- `main/knx/knx_object.cc`
- `main/knx/knx_manager.h`
- `main/knx/knx_manager.cc`
- `main/knx/knx_mcp_tools.h`
- `main/knx/knx_mcp_tools.cc`
- `main/knx/README.md`
- `docs/knx_ip_integration.md`
- `docs/knx_ip_configuration.md`
- `docs/knx_ip_mcp.md`

Modify:

- `main/idf_component.yml`: pin the actual `esp_knx_ip` component revision.
- `main/Kconfig.projbuild`: add the optional routing-mode configuration menu.
- `main/CMakeLists.txt`: compile/link KNX adapter sources only when enabled.
- `main/boards/common/board.h`: add optional native-netif capability.
- Common Wi-Fi/Ethernet board implementations: return their existing active
  native netif without changing ownership or network initialization.
- `main/application.cc` and `main/application.h`: initialize the manager and
  forward connected/disconnected lifecycle events through its non-blocking API.
- `main/mcp_server.cc`: register KNX tools through the manager-owned adapter
  when enabled.

Implementation order:

1. Pin/reconfigure the component and prove a disabled build remains unchanged.
2. Add host-testable object, address, DPT, and persistence validation.
3. Add the optional board native-netif capability for Wi-Fi and Ethernet.
4. Implement the bounded manager lifecycle worker and callback/cache path.
5. Integrate application network events and verify reconnect behavior.
6. Add bounded MCP tools and AI-oriented descriptions.
7. Add user documentation, build validation, and hardware test results.