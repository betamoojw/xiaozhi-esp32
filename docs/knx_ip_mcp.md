# KNX MCP Tools

KNX uses XiaoZhi's existing MCP server. Tools are registered only when
`CONFIG_XIAOZHI_KNX_IP` is enabled. MCP never constructs raw KNX packets or
selects an arbitrary DPT.

## `self.knx.get_status`

Returns routing state, mode, endpoint, physical address, configured and valid
object counts, last communication timestamp, and last error. Call it when KNX
availability is unknown. `running` indicates active multicast routing, not an
authenticated or confirmed gateway connection.

## `self.knx.list_objects`

Parameters:

- `offset`: optional integer, default 0.
- `limit`: optional integer from 1 through 20, default 10.

Returns concise semantic metadata and cache validity. Continue with
`next_offset` while `has_more` is true. Use this tool before guessing an object
ID from natural language.

## `self.knx.get_object`

Parameter: `object_id`.

Returns full metadata, read/write permissions, and cached state. A value is
present only when `valid` is true. If false, answer that the value is unknown;
when the object is readable, `self.knx.read` may request a refresh.

## `self.knx.read`

Parameter: `group_address`.

Validates that the address belongs to a configured readable object, sends a KNX
group read, and returns immediately with `completion: asynchronous`. It never
blocks XiaoZhi while waiting for the bus. Query the semantic object cache after
the response arrives.

## `self.knx.write`

Parameters: `group_address`, `value`.

Writes only a configured writable address using its configured DPT. This is a
lower-level fallback when an object ID is unavailable. Prefer
`self.knx.set_object`.

## `self.knx.set_object`

Parameters: `object_id`, `value`.

This is the preferred natural-language control tool. Resolve semantic names
with `self.knx.list_objects`, then pass the exact configured object ID. Use it
for switching, numeric setpoints, and other writes. Do not use it for status
questions.

## Value Input

`value` is a string because XiaoZhi's current MCP property model does not
preserve floating-point JSON numbers. Parsing remains type-safe:

- DPT 1: `true`, `false`, `on`, `off`, `1`, or `0`.
- DPT 4.001, 16, 24, 28, and 234: text. DPT 4.001 requires one ASCII
    character, DPT 16 permits 14 bytes, and DPT 234 requires a lowercase
    two-letter language code.
- DPT 5.001 and 5.003, 9, and 14: strict finite decimal numbers. Scaling and
    angle values are checked by the component codecs.
- Scalar integer families: strict decimal integers in the component type and
    codec range.
- DPT 2: `control,value`.
- DPT 3: `control,step_code`.
- DPT 10: `weekday,hour,minute,second`.
- DPT 11: `day,month,year` (two-digit year).
- DPT 18 and 26: `learn,scene_number` and `active,scene_number`.
- DPT 19: `year,month,day,weekday,hour,minute,second,fault,working_day,working_day_valid,date_valid,weekday_valid,time_valid,daylight_saving_time,clock_quality`.
- DPT 27: `value,mask`.
- DPT 232: `red,green,blue`.
- DPT 251: `red,green,blue,white,valid_channels`.

Booleans inside compound values use the same accepted spellings as DPT 1.
Cached scalar values are returned as JSON booleans, numbers, or strings;
compound values are returned as named JSON objects. DPT 29 is returned as a
decimal string to avoid JSON floating-point precision loss.

Whitespace, trailing text, unknown DPT families, overflows, unknown IDs/addresses,
and writes to read-only objects are rejected.

## Natural-Language Flow

For a control request:

```text
list objects when needed -> select exact semantic ID -> set object
```

For a status question:

```text
get object -> return cached value when valid
           -> report unknown when invalid
           -> optionally request read, then query again later
```

Object names and addresses are site configuration. The AI must not invent an
ID or substitute a similarly named object without consulting the configured
registry.

## `self.knx.import_configuration`

This owner-only commissioning tool is excluded from the AI tool audience. Its
required `configuration` property is the complete object registry encoded as a
JSON string. Validation and the atomic LittleFS commit finish before the active registry is
replaced. A successful import takes effect immediately by restarting the KNX
routing transport; a failed import leaves the previous runtime and persisted
configuration unchanged.

Use [the configuration guide](knx_ip_configuration.md) for the schema, import
procedure, size limits, and generic test registry.

## Error Behavior

Validation and transport failures become MCP tool errors with a concise reason.
No failed write changes the cache. If KNX is unavailable, other XiaoZhi MCP
tools and voice functions remain operational.