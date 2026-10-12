# Task: Integrate KNX/IP ESP-IDF Component into XiaoZhi ESP32

## 1. Objective

Enhance the XiaoZhi ESP32 firmware so that it can communicate with a KNX installation through KNX/IP and allow the XiaoZhi AI assistant to:

1. Read the current value/status of KNX Communication Objects.
2. Write/control KNX Communication Objects.
3. Expose KNX devices and Communication Objects to the XiaoZhi MCP interface.
4. Allow the AI model to understand and execute natural-language KNX commands.
5. Return KNX Communication Object values/status to the AI so that XiaoZhi can answer questions about the KNX installation.
6. Receive KNX/IP group-address updates asynchronously and maintain a local KNX object state/cache.
7. Preserve the existing XiaoZhi architecture, networking, audio, UI, MCP, and board support.

The target repositories are:

- XiaoZhi ESP32:
  https://github.com/78/xiaozhi-esp32.git
- KNX/IP ESP-IDF component:
  https://github.com/betamoojw/esp32_knx_ip.git

The implementation must use the KNX/IP repository as an **ESP-IDF component**, preferably through `idf_component.yml` / ESP-IDF Component Manager rather than copying source files into XiaoZhi.

---

# 2. Important Architecture Requirements

First inspect and understand both repositories before modifying any code.

Do NOT start by writing code.

Perform an architecture review of:

### XiaoZhi

Inspect at minimum:

- `AGENTS.md`
- `README.md`
- `main/application.*`
- `main/mcp_server.*`
- `main/Kconfig.projbuild`
- `main/CMakeLists.txt`
- board architecture under `main/boards/`
- existing MCP tool implementations
- existing network initialization/lifecycle
- configuration/settings architecture
- event/task architecture
- existing component dependencies
- `idf_component.yml`
- `sdkconfig.defaults`
- relevant documentation under `docs/`

The current XiaoZhi architecture identifies `main/application.*` as the high-level application/lifecycle layer, `main/mcp_server.*` as the device-side MCP implementation, and board-specific `InitializeTools()` as the location for custom tools. Follow this architecture rather than introducing a parallel control framework.

### KNX/IP component

Inspect the complete repository:

- component structure
- `idf_component.yml`
- `CMakeLists.txt`
- public headers
- source implementation
- KNX/IP protocol implementation
- UDP/network dependencies
- group-address handling
- datapoint handling
- communication object implementation
- callbacks/events
- initialization API
- connection lifecycle
- reconnect behavior
- memory ownership
- task/thread model
- error handling
- logging
- supported ESP-IDF versions
- supported ESP32 targets
- supported KNX datapoint types
- KNX/IP routing/tunneling behavior
- security support, if any
- licensing

Do not assume the KNX component API. Derive its actual API from the source code.

---

# 3. First Deliverable: Architecture and Compatibility Analysis

Before modifying the project, create:

`docs/knx_ip_integration_analysis.md`

Document:

1. XiaoZhi architecture relevant to KNX.
2. KNX/IP component architecture.
3. ESP-IDF version compatibility.
4. API compatibility.
5. Network-stack compatibility.
6. Task/concurrency considerations.
7. Memory/heap implications.
8. Initialization ordering.
9. Wi-Fi/Ethernet compatibility.
10. Potential conflicts with XiaoZhi networking.
11. Required component dependencies.
12. Required Kconfig options.
13. Required `sdkconfig` options.
14. Recommended integration architecture.
15. Risks and mitigations.
16. Testing strategy.

Clearly identify anything that must be modified in the KNX/IP component to make it compatible with the current XiaoZhi ESP-IDF version.

Do not duplicate the KNX/IP implementation into XiaoZhi.

---

# 4. ESP-IDF Component Integration

Integrate:

`https://github.com/betamoojw/esp32_knx_ip.git`

as a managed ESP-IDF component.

Prefer a dependency declaration similar to:

```yaml
dependencies:
  knx_ip:
    git: https://github.com/betamoojw/esp32_knx_ip.git
```

However, determine the actual component name and dependency syntax from the KNX component's `idf_component.yml`.

Do not invent the component name.

If the component is not currently compatible with ESP-IDF 6.x:

1. Identify the incompatibilities.
2. Fix them with the smallest possible changes.
3. Preserve the original KNX/IP public API wherever practical.
4. Avoid forking the implementation unnecessarily.
5. Document all compatibility changes.

The resulting XiaoZhi project must be reproducible using a clean checkout and the normal ESP-IDF build process.

---

# 5. Create a KNX Service Abstraction

Do not allow MCP code to directly manipulate low-level KNX/IP implementation details.

Create a dedicated XiaoZhi-side abstraction such as:

```text
main/knx/
├── knx_manager.h
├── knx_manager.cc
├── knx_object.h
├── knx_object.cc
├── knx_config.h
└── README.md
```

Use better names if the existing project architecture suggests another structure.

The abstraction should provide a clean API similar to:

```cpp
class KnxManager {
public:
    static KnxManager& GetInstance();

    bool Initialize();
    bool Start();
    void Stop();

    bool IsConnected() const;

    bool ReadGroupAddress(
        const std::string& group_address,
        KnxValue& value);

    bool WriteGroupAddress(
        const std::string& group_address,
        const KnxValue& value);

    bool RegisterCommunicationObject(
        const KnxCommunicationObject& object);

    bool GetCommunicationObject(
        const std::string& id,
        KnxCommunicationObject& object);

    bool GetCommunicationObjectValue(
        const std::string& id,
        KnxValue& value);

    std::vector<KnxCommunicationObject> GetObjects();

private:
    // implementation
};
```

This is an architectural example only.

Adapt the API to the actual KNX/IP component.

---

# 6. Communication Object Model

Introduce a logical KNX Communication Object model.

Each configured object should contain at least:

```text
id
name
description
group_address
datapoint_type
readable
writable
current_value
valid
last_update_timestamp
```

Example:

```json
{
  "id": "living_room_light",
  "name": "Living Room Light",
  "description": "Main ceiling light",
  "group_address": "1/0/1",
  "datapoint_type": "DPT-1",
  "readable": true,
  "writable": true,
  "current_value": true
}
```

Do not hard-code this example.

Design the actual configuration mechanism so additional KNX objects can be added without firmware recompilation if practical.

---

# 7. KNX Datapoint Support

Inspect the KNX/IP component and determine which datapoint types are actually supported.

At minimum, design the abstraction to accommodate:

- DPT 1.x — Boolean
- DPT 5.x — 8-bit values
- DPT 7.x — 16-bit unsigned values
- DPT 9.x — 2-byte floating point
- DPT 12.x — 4-byte unsigned
- DPT 13.x — 4-byte signed
- DPT 14.x — 4-byte IEEE float
- DPT 17.x — Scene number
- DPT 20.x — HVAC/enumeration values

Only implement datapoint types that can be correctly supported by the underlying KNX component.

Do not fake or approximate KNX datapoint encoding.

Document unsupported DPTs.

The abstraction should use a type-safe representation rather than passing arbitrary strings whenever possible.

---

# 8. KNX Configuration

Add Kconfig configuration for enabling/disabling KNX/IP.

For example:

```text
CONFIG_XIAOZHI_KNX_IP
CONFIG_XIAOZHI_KNX_IP_AUTO_START
CONFIG_XIAOZHI_KNX_IP_TUNNELING
CONFIG_XIAOZHI_KNX_IP_ROUTING
```

Use only options that are actually required.

Configuration should include, as appropriate:

- KNX/IP enable
- KNX interface mode
- KNX/IP server/router address
- local physical address
- individual address
- connection timeout
- reconnect interval
- maximum configured objects
- debug logging

Do not expose low-level implementation settings unless there is a clear reason.

---

# 9. Network Integration

This is a critical requirement.

XiaoZhi already supports multiple network transports, including Wi-Fi and Ethernet.

KNX/IP must operate over the existing network connection.

Do NOT create a second independent Wi-Fi manager.

Do NOT restart or reconfigure Wi-Fi when KNX starts.

Do NOT create a second DHCP client.

Do NOT conflict with XiaoZhi's existing network stack.

The KNX service should:

1. Wait for network availability.
2. Start KNX/IP when the network is ready.
3. Stop or suspend KNX/IP when the network goes down.
4. Automatically reconnect when network connectivity returns.
5. Avoid blocking the audio or main application task.
6. Work with Wi-Fi.
7. Work with Ethernet where the selected XiaoZhi board supports Ethernet.

Use the existing XiaoZhi network lifecycle/events wherever possible.

---

# 10. MCP Integration

Use XiaoZhi's existing MCP architecture.

The current project recommends MCP for new IoT control, with tools registered through `McpServer::AddTool()`.

Do NOT implement a separate AI command protocol.

Create KNX MCP tools.

Recommended initial tools:

### 10.1 Get KNX Status

```text
self.knx.get_status
```

Purpose:

Return:

- KNX/IP connection status
- local KNX address
- KNX/IP endpoint
- number of configured objects
- number of valid objects
- last communication timestamp
- errors, if any

---

### 10.2 List Communication Objects

```text
self.knx.list_objects
```

Return a concise list of configured KNX Communication Objects.

Example:

```json
[
  {
    "id": "living_room_light",
    "name": "Living Room Light",
    "group_address": "1/0/1",
    "datapoint_type": "DPT-1",
    "value": true
  }
]
```

Avoid returning unnecessarily large payloads.

---

### 10.3 Get Object Status

```text
self.knx.get_object
```

Parameters:

```text
object_id
```

Return:

- object name
- group address
- datapoint type
- current value
- value validity
- timestamp

---

### 10.4 Read Group Address

```text
self.knx.read
```

Parameters:

```text
group_address
```

Return the current KNX value.

Use this only when the underlying KNX implementation supports an appropriate read/request operation.

---

### 10.5 Write Group Address

```text
self.knx.write
```

Parameters:

```text
group_address
value
```

Validate:

- group address format
- datapoint type
- value range
- writable permission

Return a meaningful success/failure result.

---

### 10.6 Set Communication Object

Prefer a higher-level semantic API:

```text
self.knx.set_object
```

Parameters:

```text
object_id
value
```

Example:

```json
{
  "object_id": "living_room_light",
  "value": true
}
```

This should resolve the object to its configured group address and datapoint type.

---

# 11. AI-Friendly Tool Descriptions

The MCP tool descriptions are extremely important because the AI model uses them to determine when to invoke the tools.

Write descriptions that allow natural-language commands such as:

- "Turn on the living room light."
- "Turn off the bedroom light."
- "Is the living room light on?"
- "What is the temperature in the bedroom?"
- "Set the living room temperature to 22 degrees."
- "What is the current humidity?"
- "Activate scene 3."

Tool descriptions should clearly explain:

1. What the tool does.
2. Which object it operates on.
3. Which values are accepted.
4. Whether it reads or writes.
5. When the AI should call it.
6. What should happen if the value is unknown.

For example:

```text
Controls a KNX communication object.
Use this tool when the user asks to turn a KNX-controlled device on or off, change a setpoint, activate a scene, or modify another configured KNX object.
Do not use this tool for querying status; use self.knx.get_object instead.
```

---

# 12. Natural Language → KNX Mapping

The AI must be able to map semantic device names to KNX objects.

Example configuration:

```text
living_room_light
    name = "Living Room Light"
    GA = 1/0/1
    DPT = DPT-1

living_room_temperature
    name = "Living Room Temperature"
    GA = 2/1/1
    DPT = DPT-9

living_room_temperature_setpoint
    name = "Living Room Temperature Setpoint"
    GA = 2/1/2
    DPT = DPT-9
```

Then:

User:

> Turn on the living room light.

AI:

```text
self.knx.set_object
object_id = living_room_light
value = true
```

User:

> What is the living room temperature?

AI:

```text
self.knx.get_object
object_id = living_room_temperature
```

User:

> Set the living room temperature to 22 degrees.

AI:

```text
self.knx.set_object
object_id = living_room_temperature_setpoint
value = 22.0
```

Do not hard-code these objects.

---

# 13. Asynchronous KNX Status Updates

KNX status must not depend exclusively on AI polling.

When a KNX group telegram is received:

```text
KNX/IP
   ↓
KNX component
   ↓
KnxManager
   ↓
Communication Object cache
   ↓
MCP / application
```

The manager should update:

```text
current_value
valid
last_update_timestamp
```

The implementation should be thread-safe.

Avoid holding mutexes while performing network operations.

Use callbacks/events/queues according to the architecture of the KNX component and XiaoZhi.

---

# 14. State Cache

Implement a local cache of KNX Communication Object states.

Requirements:

- latest known value
- validity flag
- timestamp
- communication status
- optional quality/error state

Example:

```cpp
struct KnxObjectState {
    KnxValue value;
    bool valid;
    uint64_t timestamp;
};
```

If a value has never been received, do not report it as a valid current value.

The AI should receive:

```text
unknown
```

or an explicit invalid/unavailable state rather than a fabricated value.

---

# 15. Initialization Lifecycle

Integrate KNX startup into the XiaoZhi lifecycle.

Expected sequence:

```text
Boot
  ↓
XiaoZhi initialization
  ↓
Network initialization
  ↓
Network connected
  ↓
KNX/IP initialization
  ↓
KNX/IP connected
  ↓
Register KNX callbacks
  ↓
Load Communication Object configuration
  ↓
Register MCP tools
  ↓
Ready
```

Do not make the entire firmware boot process dependent on the KNX gateway being reachable.

If KNX/IP is unavailable:

```text
XiaoZhi continues operating normally
KNX service enters disconnected state
KNX reconnect task retries in background
```

---

# 16. Failure Handling

Handle at least:

- KNX/IP gateway unavailable
- network disconnected
- network reconnect
- UDP socket failure
- timeout
- malformed group address
- unsupported DPT
- invalid value
- write failure
- read timeout
- KNX service initialization failure
- duplicate communication object ID
- duplicate group address
- invalid configuration

Errors must not crash the XiaoZhi application.

---

# 17. Threading and Performance

Do not block:

- audio processing
- voice recognition
- TTS
- MCP message processing
- UI/LVGL tasks
- main application task

KNX network operations should run in an appropriate background task or asynchronous mechanism.

Avoid:

```cpp
while (...) {
    knx_read();
}
```

inside MCP callbacks.

MCP callbacks should be short and non-blocking where practical.

If an operation requires asynchronous completion, design an appropriate request/response mechanism.

---

# 18. Security

Do not expose arbitrary KNX/IP operations without validation.

Validate all MCP arguments.

For write operations:

- validate object ID
- validate group address
- validate DPT
- validate value
- enforce writable flag

Do not allow an AI-generated command to directly manipulate arbitrary memory or raw packet contents.

If KNX Secure is not supported by the current KNX/IP component, clearly document that limitation.

Do not claim KNX Secure support unless it is actually implemented and tested.

---

# 19. Configuration Persistence

Determine whether KNX object configuration should be stored in:

- NVS
- LittleFS
- existing XiaoZhi settings mechanism
- static configuration
- another existing configuration facility

Prefer an architecture that allows KNX objects to be configured without recompiling firmware.

If dynamic configuration is beyond the current scope, implement a clean static configuration first and document the extension path.

---

# 20. MCP Payload Size

XiaoZhi's MCP implementation has payload-size considerations and supports pagination when listing tools.

Do not expose hundreds of KNX objects as one enormous MCP response.

Design:

```text
self.knx.list_objects
```

to support reasonable limits or pagination if necessary.

Avoid putting the complete KNX database into every MCP response.

---

# 21. Logging

Use ESP-IDF logging consistently.

Example categories:

```text
KNX
KNX_MGR
KNX_MCP
```

Log:

### INFO

- KNX initialization
- gateway connection
- successful reconnect
- configuration loaded

### DEBUG

- group address
- DPT
- received values
- transmitted values

### WARN

- reconnect
- timeout
- invalid configuration

### ERROR

- initialization failure
- socket failure
- protocol failure
- unrecoverable configuration error

Do not log sensitive credentials or security keys.

---

# 22. Documentation

Create:

```text
docs/knx_ip_integration.md
docs/knx_ip_configuration.md
docs/knx_ip_mcp.md
docs/knx_ip_integration_analysis.md
```

Document:

1. Architecture.
2. Installation.
3. Component dependency.
4. Configuration.
5. KNX/IP setup.
6. Communication Object configuration.
7. MCP tools.
8. Natural-language examples.
9. Supported DPTs.
10. Troubleshooting.
11. Known limitations.
12. Testing.

---

# 23. Example User Experience

The final system should support conversations such as:

### Example 1

User:

> Turn on the living room light.

Expected:

```text
AI
 ↓
self.knx.set_object
 ↓
living_room_light
 ↓
KNX GA 1/0/1
 ↓
KNX/IP
 ↓
KNX actuator
```

---

### Example 2

User:

> Is the living room light on?

Expected:

```text
AI
 ↓
self.knx.get_object
 ↓
living_room_light
 ↓
current cached KNX state
 ↓
AI response
```

---

### Example 3

User:

> What is the bedroom temperature?

Expected:

```text
self.knx.get_object
    object_id = bedroom_temperature
```

Return:

```text
23.4 °C
```

---

### Example 4

User:

> Set the living room temperature to 21 degrees.

Expected:

```text
self.knx.set_object
    object_id = living_room_temperature_setpoint
    value = 21.0
```

---

# 24. Testing Requirements

Implement tests where practical.

At minimum validate:

## Build

- clean build
- ESP32-S3
- at least one Wi-Fi board
- at least one Ethernet-capable board if available

## KNX

- KNX/IP initialization
- gateway connection
- reconnect
- group write
- group read
- incoming telegram
- state cache update
- invalid group address
- unsupported DPT
- invalid value

## MCP

Test:

```text
tools/list
tools/call self.knx.get_status
tools/call self.knx.list_objects
tools/call self.knx.get_object
tools/call self.knx.read
tools/call self.knx.write
tools/call self.knx.set_object
```

## Network

Test:

```text
Wi-Fi connected → KNX connected

Wi-Fi disconnected → KNX disconnected

Wi-Fi reconnect → KNX reconnect

Ethernet connected → KNX connected
```

## Regression

Verify that KNX integration does not break:

- boot
- Wi-Fi provisioning
- Ethernet
- WebSocket
- MQTT/UDP
- voice capture
- ASR
- LLM interaction
- TTS
- MCP
- display
- existing board tools

---

# 25. Hardware Integration Test

If physical KNX hardware is available, test against:

```text
ESP32 XiaoZhi
      |
      | Wi-Fi/Ethernet
      |
      v
KNX/IP Router or Interface
      |
      |
      v
KNX TP Bus
      |
      +---- KNX Switch Actuator
      |
      +---- KNX Sensor
```

Verify both directions:

```text
XiaoZhi → KNX
KNX → XiaoZhi
```

For example:

```text
Voice:
"Turn on the living room light."

XiaoZhi
→ MCP
→ KNX Manager
→ KNX/IP
→ GA 1/0/1
→ KNX actuator

Physical wall switch
→ KNX bus
→ KNX/IP
→ XiaoZhi
→ Communication Object cache
```

---

# 26. Git Changes

Keep the implementation modular.

Expected categories of changes:

```text
idf_component.yml
main/knx/*
main/mcp_server.*              # only if required
main/Kconfig.projbuild
main/CMakeLists.txt
docs/*
```

Avoid unrelated modifications.

Do not modify existing board implementations unless absolutely necessary.

Do not copy the entire KNX/IP component into `main/`.

Do not introduce a second network stack.

Do not rewrite XiaoZhi's MCP implementation.

---

# 27. Backward Compatibility

When KNX is disabled:

```text
CONFIG_XIAOZHI_KNX_IP = n
```

the firmware must behave exactly as before.

KNX code should have minimal impact on:

- firmware size
- RAM
- startup time
- CPU usage
- network behavior

when disabled.

---

# 28. Production-Quality Requirements

The implementation must be:

- modular
- maintainable
- thread-safe
- non-blocking
- fault tolerant
- configurable
- testable
- documented
- compatible with XiaoZhi coding conventions
- compatible with the project's ESP-IDF version
- suitable for future expansion

Follow the existing XiaoZhi Google C++ code style and architecture.

Do not introduce unnecessary abstractions.

Prefer the smallest architecture that cleanly separates:

```text
MCP
 ↓
KNX Manager
 ↓
KNX Communication Object Model
 ↓
KNX/IP Component
 ↓
Network
 ↓
KNX/IP Router
 ↓
KNX TP
```

---

# 29. Implementation Procedure

Follow this exact workflow:

### Phase 1 — Inspect

Analyze both repositories.

Do not modify code.

Produce:

`docs/knx_ip_integration_analysis.md`

### Phase 2 — Dependency

Integrate the KNX/IP component using ESP-IDF Component Manager.

Resolve all build/API compatibility issues.

### Phase 3 — KNX Manager

Implement the XiaoZhi-side KNX abstraction.

### Phase 4 — Configuration

Implement Kconfig and Communication Object configuration.

### Phase 5 — Network Lifecycle

Connect KNX startup/reconnect to XiaoZhi network lifecycle.

### Phase 6 — State Management

Implement Communication Object state cache and asynchronous KNX updates.

### Phase 7 — MCP

Implement the KNX MCP tools.

### Phase 8 — Documentation

Create the KNX architecture/configuration/MCP documentation.

### Phase 9 — Build

Perform clean builds.

### Phase 10 — Test

Run automated tests and hardware tests where available.

### Phase 11 — Review

Perform a final code review focused on:

- race conditions
- memory leaks
- deadlocks
- blocking calls
- invalid MCP arguments
- network reconnect
- task lifetime
- component dependency correctness
- firmware size
- backward compatibility

---

# 30. Final Deliverables

At the end of the implementation, provide:

1. Modified source code.
2. KNX/IP component dependency configuration.
3. KNX Manager.
4. Communication Object model.
5. MCP tools.
6. Kconfig configuration.
7. Documentation.
8. Unit/integration tests where practical.
9. Build results.
10. Hardware test results.
11. List of modified files.
12. Known limitations.
13. Recommended next steps.

Also provide a final architecture diagram similar to:

```text
                    ┌─────────────────────┐
                    │   XiaoZhi AI/LLM    │
                    └──────────┬──────────┘
                               │
                               │ MCP
                               ▼
                    ┌─────────────────────┐
                    │    McpServer        │
                    │  KNX MCP Tools      │
                    └──────────┬──────────┘
                               │
                               ▼
                    ┌─────────────────────┐
                    │    KnxManager       │
                    │                     │
                    │ Object Registry     │
                    │ State Cache         │
                    │ Validation          │
                    │ Reconnect           │
                    └──────────┬──────────┘
                               │
                               ▼
                    ┌─────────────────────┐
                    │  esp32_knx_ip       │
                    │  ESP-IDF Component  │
                    └──────────┬──────────┘
                               │
                         Wi-Fi/Ethernet
                               │
                               ▼
                    ┌─────────────────────┐
                    │    KNX/IP Router    │
                    └──────────┬──────────┘
                               │
                            KNX TP
                               │
             ┌─────────────────┼─────────────────┐
             ▼                 ▼                 ▼
        KNX Actuator       KNX Sensor       KNX Panel
```

---

# 31. Important Agent Rules

- Inspect before modifying.
- Do not guess APIs.
- Do not duplicate the KNX component.
- Do not create a second network stack.
- Do not block XiaoZhi audio/MCP/application tasks.
- Do not bypass the MCP architecture.
- Do not hard-code KNX Communication Objects into MCP callbacks.
- Do not report unknown KNX values as valid.
- Do not silently ignore KNX connection failures.
- Do not break existing XiaoZhi board support.
- Do not modify unrelated code.
- Prefer ESP-IDF Component Manager dependency management.
- Follow the current XiaoZhi architecture and coding conventions.
- Run a clean build after integration.
- Report every compatibility change.
- Clearly distinguish "build verified" from "hardware verified."

The implementation is considered complete only when XiaoZhi can reliably perform both:

**AI → KNX control**

and

**KNX → XiaoZhi status feedback**

through KNX/IP while preserving the existing XiaoZhi functionality.