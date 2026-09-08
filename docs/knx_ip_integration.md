# KNX/IP Integration

## Overview

XiaoZhi can expose configured KNX communication objects to its existing MCP
server through the managed `esp_knx_ip` component. The integration uses the
network connection already established by the selected board.

```text
AI -> MCP -> KnxManager -> esp_knx_ip -> Wi-Fi/Ethernet -> KNX/IP router
```

Enable `CONFIG_XIAOZHI_KNX_IP` and configure the object registry described in
[knx_ip_configuration.md](knx_ip_configuration.md). KNX is disabled by default;
disabled builds do not compile the adapter sources or create KNX tasks.

## Component Dependency

`main/idf_component.yml` pins `esp_knx_ip` from
`https://github.com/betamoojw/esp32_knx_ip.git`, path
`components/esp_knx_ip`, at the reviewed immutable revision. Component Manager
fetches it during reconfigure/build. Do not copy component source into XiaoZhi.

The upstream component requires ESP-IDF 5.0 or newer. XiaoZhi targets ESP-IDF
6.0.2 and retains 5.5.2 for legacy boards. A build under the selected SDK is
still required for each release variant.

## Lifecycle

1. XiaoZhi initializes NVS, UI, audio, and MCP.
2. `KnxManager` loads and validates the object registry and starts a low-priority
   lifecycle task.
3. The selected board establishes Wi-Fi or Ethernet as usual.
4. On a connected event, XiaoZhi passes the board's existing native netif to
   the manager.
5. The manager creates the KNX handle, registers unique group addresses, binds
   UDP port 3671, and joins `224.0.23.12` by default.
6. On disconnect, the lifecycle task stops and destroys the handle. Reconnect
   creates a new handle and multicast membership for the current IP address.

Initialization, socket, configuration, and router failures do not stop audio,
UI, XiaoZhi protocols, or other MCP tools. Status and the last error remain
available through `self.knx.get_status`.

## Routing Behavior

The component implements KNXnet/IP routing indications over IPv4 multicast.
The configured individual address is the source address for outgoing group
telegrams. Incoming write and response telegrams update all compatible objects
registered for the destination group address.

`running` means the UDP socket is bound and multicast membership succeeded. It
does not prove that a KNX router or TP bus is reachable because routing is
connectionless.

Group reads are asynchronous. `self.knx.read` sends a read telegram and returns
immediately; a later response updates the cache. Query the semantic object with
`self.knx.get_object`. A value remains invalid until a write or response was
actually received.

## Supported Networks

- Common XiaoZhi Wi-Fi boards use the existing `WIFI_STA_DEF` netif.
- Common Ethernet boards return their existing Ethernet netif.
- Dual Wi-Fi/cellular boards support KNX only while Wi-Fi is selected.
- Cellular, USB-hosted, remote Wi-Fi, and custom boards that do not expose a
  native ESP-IDF IPv4 netif report KNX as unavailable.

The integration does not create another network manager, event loop, DHCP
client, or Wi-Fi station.

## Supported Datapoints

| Family | Value | Accepted write text |
| --- | --- | --- |
| DPT 1.x | Boolean | `true`, `false`, `on`, `off`, `1`, `0` |
| DPT 5.x | Unsigned 8-bit | `0` through `255` |
| DPT 7.x | Unsigned 16-bit | `0` through `65535` |
| DPT 9.x | KNX two-byte float | Finite decimal in component range |
| DPT 12.x | Unsigned 32-bit | `0` through `4294967295` |
| DPT 13.x | Signed 32-bit | `-2147483648` through `2147483647` |
| DPT 14.x | IEEE-754 float | Finite decimal |

DPT 5 and 7 are raw values. The manager does not infer percentage scaling,
units, or subtype ranges. DPT 17 scenes and DPT 20 enums are not implemented by
the component and are rejected. Other upstream codecs are not exposed until a
stable MCP value schema is defined.

## Security and Limitations

- KNX/IP tunneling is not supported.
- KNX Secure, authentication, and encryption are not supported.
- Gateway discovery and automatic tunneling sessions are not supported.
- Deploy only on a trusted LAN with a routing-capable KNX/IP router.
- Raw APDU access is not exposed. Writes must resolve to configured writable
  objects and pass group-address, DPT, and range validation.
- The upstream repository did not contain explicit license terms at the
  reviewed revision. Clarify licensing before distributing production images.

## Validation

Run the canonical board build after sourcing ESP-IDF:

```sh
python3 scripts/build.py --list-boards
python3 scripts/build.py <wifi-board> --name <knx-enabled-variant>
python3 scripts/build.py <ethernet-board> --name <knx-enabled-variant>
e.g. python3 .\scripts\build.py lckfb/szpi-esp32s3

python3 -m unittest discover -s scripts/tests -v
```

Hardware validation must cover outgoing read/write, incoming response/write,
cache updates, Wi-Fi and Ethernet reconnect, DHCP address change, unavailable
router behavior, and concurrent audio/MCP use. A successful build is not KNX
bus validation.

The object/DPT layer also has a standalone C++17 host test. Point it at a clean
checkout of the pinned upstream repository:

```sh
cmake -S main/knx/test -B build/knx-host \
   -DESP_KNX_IP_ROOT=/path/to/esp32_knx_ip
cmake --build build/knx-host
ctest --test-dir build/knx-host --output-on-failure
```