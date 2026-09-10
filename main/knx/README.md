# XiaoZhi KNX Module

This module adapts the managed `esp_knx_ip` component to XiaoZhi. It owns the
communication-object registry, type-safe DPT conversion, state cache, routing
lifecycle worker, and MCP adapter.

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

The communication-object registry is loaded from NVS when a runtime
configuration exists, otherwise from the read-only factory Assets image.
Commissioning uses the owner-only `self.knx.import_configuration` MCP tool.
MCP imports validate and commit the complete registry to NVS before changing
the active registry. Successful imports are applied immediately and survive
reboot.

See [the integration guide](../../docs/knx_ip_integration.md),
[configuration reference](../../docs/knx_ip_configuration.md), and
[MCP reference](../../docs/knx_ip_mcp.md).