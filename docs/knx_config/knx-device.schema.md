# KNX Device JSON Schema

## 1. Overview

This schema represents a KNX device and its communication objects in JSON. It separates:

- **Common device properties**: `media_type`, `media_parameters`, `physical_address`, and `communication_objects`.
- **Media-specific configuration**: `media_parameters`.
- **Communication-object metadata and state**: the objects in `communication_objects`.

The schema uses **JSON Schema Draft 2020-12** and is intended to work with standard JSON Schema validators.

The schema file is:

`knx-device.schema.json`

A matching example is:

`knx-device.example.json`

## 2. Top-level object structure

A device has four required properties:

| Property | Type | Required | Purpose |
|---|---|---:|---|
| `media_type` | string | yes | Selects the KNX communication medium. |
| `media_parameters` | object | yes | Parameters associated with the selected medium. |
| `physical_address` | string | yes | KNX individual/physical address. |
| `communication_objects` | array | yes | Communication objects exposed by the device. |

The schema currently recognizes `knx_tp`, `knx_ip`, and `knx_rf`.

`additionalProperties` is disabled at the top level so accidental top-level properties are detected. Communication objects and media parameter objects are deliberately more extensible because implementations commonly need vendor- or deployment-specific metadata.

## 3. Supported KNX media types

### `knx_tp`

KNX Twisted Pair.

Example:

```json
{
  "media_type": "knx_tp",
  "media_parameters": {
    "line": 2,
    "coupler_role": "none",
    "topology": "standard",
    "segment": 0
  }
}
```

Supported schema-defined parameters:

- `line`: integer 0–15.
- `coupler_role`: `none`, `line_coupler`, `area_coupler`, or `repeater`.
- `topology`: `standard` or `free_topology`.
- `segment`: integer 0–3.

These are intentionally configuration-level fields rather than an attempt to model every TP installation detail.

### `knx_ip`

KNXnet/IP.

Required:

- `ip_address`: IPv4 or IPv6 address.
- `port`: TCP/UDP port number, 1–65535.
- `transport_mode`: `tunneling` or `routing`.

Optional:

- `interface_identifier`
- `individual_address`
- `host`
- `multicast_address`
- `nat`

`port` is normally 3671 for KNXnet/IP, but the schema permits the full valid port range because installations and gateways can use different configured ports.

`individual_address` is an exception to the general no-duplication rule. It should only be used when an implementation genuinely needs to record an interface-assigned individual address. The authoritative device address remains `physical_address`.

### `knx_rf`

KNX Radio Frequency.

Supported schema-defined parameters:

- `rf_domain_address`: opaque RF domain identifier.
- `channel`: integer 0–255.
- `mode`: `system_mode`, `ready_mode`, or `other`.
- `repeaters_enabled`: boolean.

RF deployments can have profile-specific parameters, so `media_parameters` permits additional properties.

## 4. Physical address format

`physical_address` uses the standard three-part KNX individual-address notation:

`area.line.device`

Constraints:

- area: 0–15
- line: 0–15
- device: 0–255

Examples:

- `1.2.15`
- `0.0.1`
- `15.15.255`

The JSON Schema regular expression validates the numeric ranges as well as the separators.

The physical address is a common device property and should not normally be copied into `media_parameters`.

## 5. Communication object definition

Every communication object requires:

- `id`
- `name`
- `description`
- `group_address`
- `datapoint_type`
- `readable`
- `writable`
- `current_value`
- `valid`
- `last_update_timestamp`

Additional communication-object properties are permitted so implementations can add metadata such as units, tags, source information, priorities, flags, or vendor-specific fields.

## 6. Field-by-field reference

### `id`

String, 1–128 characters.

It must match:

`^[A-Za-z0-9][A-Za-z0-9._:-]*$`

It is the object's identifier within the device. The recommended uniqueness rule is that no two communication objects in one device have the same `id`.

### `name`

Non-empty human-readable string, maximum 256 characters.

### `description`

String, maximum 2000 characters. It is required in the base schema to keep the object contract explicit, but an empty string can be used when no description exists.

### `group_address`

String in three-level notation:

`main.middle.subgroup`

Ranges:

- main: 0–31
- middle: 0–31
- subgroup: 0–255

Examples:

- `1.0.1`
- `2.1.10`
- `31.31.255`

This schema intentionally standardizes the textual three-level representation rather than accepting slash notation as a second canonical form.

### `datapoint_type`

String in canonical form:

`DPT-main.subtype`

Examples:

- `DPT-1.001`
- `DPT-5.001`
- `DPT-9.001`
- `DPT-14.000`
- `DPT-17.001`

The schema validates the syntax:

`^DPT-[0-9]{1,2}\.[0-9]{3}$`

The syntax check does not attempt to contain the complete KNX DPT registry. Consequently, a syntactically valid but unsupported DPT number can pass JSON Schema validation.

Applications that require a closed DPT catalogue should validate the identifier against their KNX DPT catalogue as a second semantic-validation step.

## 7. Datatype and validation rules

| Field | JSON type | Validation |
|---|---|---|
| `media_type` | string | One of `knx_tp`, `knx_ip`, `knx_rf`. |
| `media_parameters` | object | Shape is selected by `media_type`. |
| `physical_address` | string | `area.line.device`, ranges 0–15/0–15/0–255. |
| `communication_objects` | array | Each item is a communication object. |
| `id` | string | 1–128 chars; restricted identifier pattern. |
| `name` | string | 1–256 chars. |
| `description` | string | max 2000 chars. |
| `group_address` | string | `main.middle.subgroup`, ranges 0–31/0–31/0–255. |
| `datapoint_type` | string | `DPT-[0-9]{1,2}.[0-9]{3}`. |
| `readable` | boolean | JSON boolean only. |
| `writable` | boolean | JSON boolean only. |
| `current_value` | null/bool/number/string/array/object | Flexible JSON representation. |
| `valid` | boolean | JSON boolean only. |
| `last_update_timestamp` | string | RFC 3339 `date-time`. |

## 8. KNX group address format

The canonical representation is:

`main.middle.subgroup`

The schema uses:

- main = 0–31
- middle = 0–31
- subgroup = 0–255

For example, `2.1.10` represents main group 2, middle group 1, subgroup 10.

If an implementation needs two-level group addresses, it should normalize them into the canonical three-level form before storing them in this schema.

## 9. KNX datapoint type representation

DPT identifiers are strings rather than numbers because the subtype component contains a significant separator and leading zeroes.

Use:

```text
DPT-1.001
DPT-5.001
DPT-9.001
```

rather than:

```text
1.1
5.1
9.1
```

The schema validates the identifier's syntax but does not hard-code the complete KNX DPT catalogue. This avoids making the schema unnecessarily brittle as KNX DPT definitions evolve.

## 10. `current_value` representation

`current_value` is deliberately not restricted to a single JSON type.

It can be:

- `boolean` for boolean DPTs, such as `DPT-1.xxx`.
- `number` for numeric DPTs, such as percentage, float, or signed integer representations.
- `string` for textual or enumerated representations where the application chooses a string representation.
- `array` or `object` for compound/structured DPTs.
- `null` when no value has been received or no value is currently available.

Examples:

```json
{ "datapoint_type": "DPT-1.001", "current_value": true }
```

```json
{ "datapoint_type": "DPT-5.001", "current_value": 72.5 }
```

```json
{
  "datapoint_type": "DPT-14.000",
  "current_value": 21.7
}
```

```json
{
  "datapoint_type": "DPT-19.001",
  "current_value": {
    "date": "2026-09-09",
    "time": "20:30:00",
    "day_of_week": 3
  }
}
```

The schema cannot generically prove that a particular `current_value` is semantically correct for every possible DPT. That relationship is a cross-field semantic constraint. An application-level validator should maintain a DPT catalogue and check `datapoint_type` against the corresponding value shape/range.

For example, an application can enforce that `DPT-1.001` is boolean and that `DPT-5.001` is a value in the DPT's defined range. JSON Schema itself is used here to guarantee that the value is valid JSON and belongs to an allowed broad representation class.

## 11. Timestamp requirements

`last_update_timestamp` uses JSON Schema's `format: date-time`.

It should contain an RFC 3339 timestamp, preferably with an explicit UTC offset or `Z`.

Example:

`2026-09-09T20:30:12-05:00`

A JSON Schema validator's format enforcement can be configured differently, so applications that require strict timestamp validation should enable format assertion/checking in their validator.

## 12. Uniqueness of communication object IDs

Each communication object has an `id` that is intended to be unique within the device.

JSON Schema Draft 2020-12 does not provide a simple built-in "unique value of property X across all array objects" keyword.

Therefore, this rule requires an application-level semantic check:

```text
for every pair of communication objects i != j:
    communication_objects[i].id != communication_objects[j].id
```

`uniqueItems: true` is also present, but that only prevents two entire array items from being identical; it does not, by itself, guarantee unique `id` values.

## 13. Complete example

See `knx-device.example.json`. In summary, the example contains:

- a KNXnet/IP device at physical address `1.2.15`;
- a tunneling interface at `192.168.10.25:3671`;
- four communication objects;
- boolean, percentage, temperature, and scene datapoint examples.

## 14. Extensibility guidance

### Future media types

The top-level structure does not need to change to add another medium.

To formally support a new medium, add a new value to `media_type` and add a corresponding conditional schema for `media_parameters`.

For example:

```json
{
  "media_type": "future_medium",
  "media_parameters": {
    "future_specific_setting": "..."
  }
}
```

The current schema includes an extensible fallback object for future/unknown media types. A deployment that requires strict closed-world validation should instead reject unknown media types until their schemas are added.

### Additional media parameters

The three media parameter definitions use `additionalProperties: true`. This permits vendor- or project-specific settings without changing the common device structure.

### Additional communication-object metadata

Communication objects also allow additional properties. This is intentional. Common additions include:

- `unit`
- `tags`
- `priority`
- `status_flags`
- `source`
- `manufacturer_metadata`

Such additions should not duplicate `group_address`, `datapoint_type`, or the common state fields.

### Versioning

The schema `$id` should be changed to the organization's stable schema URI. If breaking changes are introduced, publish a new schema version rather than silently changing the meaning of existing properties.

## 15. Semantic validation not fully expressible in standard JSON Schema

The following rules are documented separately because expressing them generically would either require an enormous DPT-specific schema or application-specific knowledge:

1. `communication_objects[*].id` values must be unique within the array.
2. `current_value` must semantically match `datapoint_type`.
3. `current_value` must satisfy the numeric/range/enum constraints of the selected DPT.
4. `rf_domain_address`, RF channel semantics, and some KNX RF profile rules depend on the RF profile and implementation.
5. `multicast_address` should be validated as an appropriate multicast IP address when `transport_mode` is `routing`.
6. `individual_address` under KNXnet/IP should only be populated where the interface actually has a distinct address that needs to be recorded.

These are intentionally left as application-level validation rather than introducing implementation-specific assumptions into the base schema.
