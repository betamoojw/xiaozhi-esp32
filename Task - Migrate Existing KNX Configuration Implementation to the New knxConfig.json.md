# Task: Migrate Existing KNX Configuration Implementation to the New `knxConfig.json`

## Objective

The current KNX implementation is based on the legacy `knxConfig_old.json` configuration format. Update and migrate the necessary source code, configuration handling, and project documentation to fully support the new `knxConfig.json` format.

The goal is to ensure that the entire KNX configuration workflow is consistent with the new configuration schema, without breaking existing KNX functionality or introducing regressions.

## Requirements

### 1. Analyze the Existing Implementation

- Inspect the entire repository and identify all source code, configuration files, and documentation that reference `knxConfig_old.json`.
- Review the current KNX configuration schema, parsing logic, validation, storage, loading, and runtime application.
- Identify all differences between `knxConfig_old.json` and the new `knxConfig.json`.
- Trace all code paths that depend on the legacy configuration format.

### 2. Update Source Code

Modify all necessary source files to support the new `knxConfig.json` schema.

Ensure that:

- The new configuration structure is correctly parsed and validated.
- All configuration fields, data types, and nested structures match the new schema.
- KNX configuration loading and initialization use the new format.
- Configuration persistence and updates work correctly.
- Runtime KNX communication and routing behavior remain functional.
- Any outdated data structures, constants, field mappings, or helper functions are updated accordingly.
- No obsolete references to `knxConfig_old.json` remain in active implementation code unless explicitly required for backward compatibility.

### 3. Update Configuration Storage and Persistence

Review and update all configuration file handling related to:

- Configuration file paths.
- LittleFS or other filesystem access.
- Reading and writing `knxConfig.json`.
- Configuration validation before persistence.
- Configuration import/export workflows.
- Runtime configuration reload and application.

Ensure that the implementation uses the correct configuration path and filesystem behavior defined by the current project architecture.

### 4. Update Documentation

Update all relevant project documentation to reflect the new `knxConfig.json` format, including:

- Configuration format descriptions.
- KNX setup and commissioning instructions.
- Configuration examples.
- Developer documentation.
- API or MCP tool documentation, if applicable.
- Migration notes, if necessary.

Remove or revise outdated references to `knxConfig_old.json`.

Documentation must accurately describe the actual implementation and must not contain assumptions or unsupported behavior.

### 5. Preserve Existing Behavior

Do not change existing KNX functionality unrelated to this migration.

Preserve:

- KNXnet/IP communication behavior.
- KNX group address handling.
- Communication-object registration.
- Configuration validation behavior, unless required by the new schema.
- Configuration import and persistence behavior.
- Runtime routing and restart behavior.
- Existing public APIs and interfaces, where compatibility is possible.

If the new configuration format requires behavior changes, document them clearly and implement them consistently.

### 6. Backward Compatibility

Determine whether backward compatibility with `knxConfig_old.json` is required by the current project.

- If backward compatibility is required, implement a safe migration or conversion mechanism.
- If backward compatibility is not required, remove obsolete legacy handling cleanly.
- Do not silently discard configuration data during migration.
- Document any breaking changes or required user actions.

## Validation and Testing

After making the changes:

1. Search the repository for remaining references to `knxConfig_old.json`.
2. Verify that all active code paths use the new `knxConfig.json` schema.
3. Build the affected ESP-IDF project successfully.
4. Run available unit tests, integration tests, and KNX-related tests.
5. Validate configuration parsing and schema compatibility.
6. Verify configuration persistence and loading.
7. Check that KNX runtime behavior remains functional.
8. Review the final diff for unintended changes or regressions.

If hardware testing is unavailable, clearly identify which validations were performed and which remain unverified.

## Deliverables

Provide:

1. A summary of all source code changes.
2. A list of updated files.
3. A list of updated documentation.
4. A detailed explanation of the migration from `knxConfig_old.json` to `knxConfig.json`.
5. Build and test results.
6. Any backward compatibility considerations.
7. Any remaining risks, limitations, or recommended follow-up work.

## Important Constraints

- Do not modify unrelated functionality.
- Do not introduce dummy files or unnecessary workarounds.
- Do not hardcode configuration values that should come from `knxConfig.json`.
- Do not claim tests passed unless they were actually executed.
- Follow the existing project coding style and architecture.
- Keep the implementation production-ready and maintainable.

## Final Acceptance Criteria

The task is complete only when the source code, configuration handling, and documentation are aligned with the new `knxConfig.json` format, the project builds successfully, and all relevant tests and validation steps have been completed or clearly reported.