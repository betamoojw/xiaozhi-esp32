## Objective

Design and implement a JSON-based configuration/schema for representing KNX devices and their communication objects.

The schema must support multiple KNX media types, their media-specific parameters, the device's physical address, and an array of KNX communication objects.

### 1. KNX Device JSON Structure

Create a JSON schema that represents a KNX device with, at minimum, the following information:

* `media_type` — the KNX communication medium.
* `media_parameters` — parameters specific to the selected media type.
* `physical_address` — the KNX physical address of the device.
* `communication_objects` — an array containing the device's KNX communication objects.

The schema must support at least these KNX media types:

* `knx_tp` — KNX Twisted Pair
* `knx_ip` — KNXnet/IP
* `knx_rf` — KNX Radio Frequency

The design should allow additional KNX media types to be added in the future without requiring a redesign of the overall schema.

### 2. Media-Specific Parameters

Define a `media_parameters` structure whose fields depend on the selected `media_type`.

For example:

#### `knx_tp`

Support parameters relevant to KNX Twisted Pair communication, such as line/coupler-related configuration where applicable.

#### `knx_ip`

Support parameters relevant to KNXnet/IP communication, such as:

* IP address
* port
* transport mode
* tunneling/router configuration
* interface identifier, if applicable

#### `knx_rf`

Support parameters relevant to KNX RF communication, such as RF-specific addressing/channel or configuration parameters where applicable.

Clearly distinguish common device properties from media-specific properties.

### 3. Physical Address

The device must contain a `physical_address` field representing its KNX individual/physical address.

The schema should:

* Define the expected format.
* Validate that the address conforms to the KNX physical address structure.
* Avoid duplicating the physical address inside media-specific parameters unless there is a specific technical reason.

### 4. KNX Communication Objects

The device must contain a `communication_objects` array.

Each communication object must contain **at least** the following fields:

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

Define the type, purpose, required/optional status, and validation rules for every field.

Recommended interpretation:

* `id` — unique identifier of the communication object within the device.
* `name` — human-readable object name.
* `description` — optional human-readable description.
* `group_address` — KNX group address associated with the object.
* `datapoint_type` — KNX datapoint type (DPT), including the required format.
* `readable` — whether the object supports KNX read requests.
* `writable` — whether the object supports KNX write requests.
* `current_value` — most recently known value of the communication object.
* `valid` — indicates whether `current_value` is currently considered valid.
* `last_update_timestamp` — timestamp of the most recent value update.

The schema should support different DPTs without assuming that `current_value` is always a string or number. Define an appropriate representation that can accommodate different KNX datapoint types.

### 5. Validation Requirements

The schema must provide validation rules for:

* Supported `media_type` values.
* Required fields.
* KNX physical address format.
* KNX group address format.
* KNX datapoint type format.
* Boolean fields such as `readable`, `writable`, and `valid`.
* Timestamp format.
* Unique communication object IDs.
* Appropriate value representation for different datapoint types.

Where a validation rule cannot be expressed directly in standard JSON Schema, document the rule separately.

### 6. JSON Schema Version

Use a current, well-supported JSON Schema specification and explicitly declare the `$schema` version in the schema document.

Prefer a design that is compatible with standard JSON Schema validators.

### 7. Deliverables

Produce the following artifacts:

#### A. JSON Schema

Create a complete JSON Schema defining the KNX device configuration structure.

Suggested filename:

```text
knx-device.schema.json
```

#### B. Example JSON Configuration

Create at least one realistic example JSON document that validates against the schema.

Suggested filename:

```text
knx-device.example.json
```

The example should demonstrate:

* A KNX device.
* Its media type.
* Media-specific parameters.
* Physical address.
* Multiple communication objects.
* Different datapoint types where practical.

#### C. Schema Documentation

Create documentation describing the schema and its usage.

Suggested filename:

```text
knx-device.schema.md
```

The documentation must include:

1. Overview
2. Top-level object structure
3. Supported KNX media types
4. Media-specific parameters
5. Physical address format
6. Communication object definition
7. Field-by-field reference
8. Datatype and validation rules
9. KNX group address format
10. KNX datapoint type representation
11. `current_value` representation
12. Timestamp requirements
13. Complete example
14. Extensibility guidance

### 8. Design Principles

Follow these principles:

* Keep common KNX device properties separate from media-specific properties.
* Use explicit and descriptive property names.
* Avoid unnecessary duplication.
* Make required versus optional fields explicit.
* Prefer machine-readable validation over documentation-only constraints where possible.
* Ensure the schema can be extended with future KNX media types and additional communication-object metadata.
* Maintain compatibility with standard JSON Schema tooling.
* Do not introduce implementation-specific assumptions unless they are clearly documented.
* Keep the resulting JSON easy for both humans and software agents to read and process.

### 9. Acceptance Criteria

The implementation is considered complete when:

* [ ] A valid JSON Schema is provided.
* [ ] `knx_tp`, `knx_ip`, and `knx_rf` are supported.
* [ ] Media-specific parameters are represented separately from common device properties.
* [ ] A KNX physical address is represented and validated.
* [ ] `communication_objects` is an array.
* [ ] Every communication object supports all required fields listed above.
* [ ] Group addresses and physical addresses have defined validation rules.
* [ ] Datapoint types are explicitly represented.
* [ ] Different KNX datapoint value types can be represented correctly.
* [ ] Read/write capabilities are represented as booleans.
* [ ] Value validity and update timestamps are represented.
* [ ] An example JSON document validates against the schema.
* [ ] Complete schema documentation is provided.
* [ ] The design is extensible for future KNX media types and communication-object properties.
