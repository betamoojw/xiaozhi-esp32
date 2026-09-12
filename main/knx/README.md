# XiaoZhi KNX Module

This module adapts the managed `esp_knx_ip` component to XiaoZhi. It owns the
communication-object registry, type-safe DPT conversion, state cache, routing
lifecycle worker, and MCP adapter.

The adapter supports every codec family exposed by the managed component:
DPT 1 through 31, 232, 234, and 251. It preserves configured subtype numbers
and delegates wire validation and conversion to `esp_knx_ip`; DPT 4.001,
5.001, and 5.003 select the component's ASCII, scaling, and angle helpers.

Dependency direction:

```text
MCP -> KnxManager -> esp_knx_ip -> existing esp_netif
```

The module never initializes Wi-Fi, Ethernet, DHCP, or an ESP-IDF event loop.
`Application` supplies connected/disconnected events and the active board
supplies an optional native `esp_netif_t` capability.

The receive callback runs on the component's task. It only decodes telegrams
and updates the mutex-protected cache. Socket start, stop, and restart run on
the `knx_lifecycle` task. MCP reads return cached state and group reads are
asynchronous, so XiaoZhi's main task never waits for a KNX response.

The communication-object registry is loaded from
`/littlefs/interfaces/knxConfig.json`. On the first boot after upgrading from
the older NVS-backed implementation, an existing NVS registry is validated and
migrated to LittleFS when the runtime file does not exist. A fresh device
creates an empty registry. Commissioning uses the owner-only
`self.knx.import_configuration` MCP tool. MCP imports validate and atomically
commit the complete registry to LittleFS before changing the active registry.
Successful imports are applied immediately and survive reboot. FTP commissioning
uploads to `knxConfig.json.tmp`, then uses the owner-only
`self.knx.import_configuration_file` tool to validate, persist, publish, and
activate it.

See [the integration guide](../../docs/knx_ip_integration.md),
[configuration reference](../../docs/knx_ip_configuration.md), and
[MCP reference](../../docs/knx_ip_mcp.md).