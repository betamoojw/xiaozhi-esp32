# Task: Add FTP Server Feature to `xiaozhi-esp32`

## Objective

Inspect the `feat/knx_ip` branch of the following repository:

* https://github.com/betamoojw/xiaozhi-esp32

The branch appears to already support a **SPIFFS-based filesystem**. Add a new **`ftp_server` feature** that exposes the existing SPIFFS filesystem through FTP, using the existing **`espp/ftp` component**, while preserving all existing system behavior and configuration.

The final implementation must allow the device to communicate with standard FTP clients such as **FileZilla** for browsing and transferring files.

---

## Requirements

### 1. Inspect the Existing SPIFFS Implementation

Before making changes:

1. Check out and inspect the `feat/knx_ip` branch.
2. Identify:

   * How SPIFFS is configured and mounted.
   * The SPIFFS mount point/path.
   * Existing filesystem helper APIs.
   * Existing partition configuration.
   * Existing component structure and conventions.
   * How features such as `led` and `notify` are organized.
3. Determine the correct location and integration pattern for the new `ftp_server` feature.

Do **not** redesign or replace the existing filesystem implementation.

---

### 2. Add the `ftp_server` Feature

Add a new feature/component named:

```text
ftp_server
```

Follow the project's existing feature organization and conventions, including its placement alongside features such as `led` and `notify` where appropriate.

Use the existing:

```text
espp/ftp
```

component rather than implementing a new FTP server from scratch.

The FTP server should operate directly on the existing SPIFFS filesystem.

Conceptually:

```text
xiaozhi-esp32
       │
       ├── existing application/features
       │
       ├── SPIFFS
       │     └── existing filesystem
       │
       └── ftp_server
              └── espp/ftp
                    └── SPIFFS files
```

---

### 3. Preserve Existing System Behavior

This is an important constraint.

**Do not change existing system behavior unless required to add the FTP functionality.**

In particular:

* Do not change the existing SPIFFS behavior.
* Do not replace SPIFFS with another filesystem.
* Do not change existing filesystem paths unnecessarily.
* Do not change application behavior.
* Do not change existing configuration semantics.
* Do not change existing partition layouts.
* Do not increase, shrink, or rearrange partitions.
* Do not introduce a new partition solely for FTP.
* Do not break existing features that use the filesystem.
* Do not modify unrelated KNX/IP functionality.
* Preserve existing build configurations and defaults.

The new FTP server must be an additive feature.

---

### 4. Filesystem Operations

The FTP implementation should support normal filesystem operations against the existing SPIFFS filesystem, including at minimum:

* List directories
* List files
* Create directories
* Delete directories
* Create files
* Delete files
* Read/view files
* Download files
* Upload files
* Rename/move files where supported
* Rename/move directories where supported
* Overwrite existing files where supported
* Navigate between directories

Use the capabilities provided by `espp/ftp` and the underlying filesystem rather than creating duplicate filesystem logic.

If SPIFFS imposes limitations on a particular operation, respect those limitations and document them rather than implementing an unsafe workaround.

---

### 5. FileZilla Compatibility

The resulting FTP server must work with a standard FTP client, specifically:

**FileZilla**

The intended workflow is:

```text
FileZilla
   │
   │ FTP
   ▼
xiaozhi-esp32
   │
   ▼
ftp_server
   │
   ▼
espp/ftp
   │
   ▼
SPIFFS
```

A user should be able to connect from FileZilla and:

1. Establish an FTP connection to the device.
2. Authenticate according to the FTP server's supported configuration.
3. Browse the SPIFFS filesystem.
4. List directories/files.
5. Download files from the device.
6. Upload files to the device.
7. Create directories.
8. Delete files/directories.
9. Rename files/directories when supported.
10. View/use normal FTP client functionality without protocol incompatibilities.

Verify that the implementation works with FileZilla rather than only testing against a custom FTP client.

---

## Configuration

Follow the existing project's configuration architecture.

If the project uses Kconfig/menuconfig for feature configuration:

* Add appropriate `ftp_server` configuration options.
* Follow existing naming conventions.
* Provide sensible defaults.
* Avoid changing existing configuration options.
* Keep the FTP feature independently enableable/disableable if that matches the project's existing feature architecture.

Potential configuration may include:

* FTP server enable/disable.
* FTP port.
* Username/password if required by `espp/ftp`.
* Other options already exposed by the existing FTP component.

Do not introduce configuration options that duplicate functionality already provided by `espp/ftp`.

---

## Networking

Use the existing network stack and network connectivity already provided by `xiaozhi-esp32`.

Do not create a separate networking architecture.

The FTP server should start only when the required network interface is available and should integrate cleanly with the existing application lifecycle.

Avoid blocking the main application/task execution.

The FTP server must not interfere with:

* Audio processing
* UI
* KNX/IP functionality
* Existing network services
* Notifications
* LED behavior
* Other application tasks

Follow the project's existing task/thread/lifecycle patterns.

---

## Dependency Management

Use the existing `espp/ftp` component through the project's normal ESP-IDF/component dependency mechanism.

Do not copy the FTP implementation into the application unless the repository's dependency architecture explicitly requires it.

Inspect the existing `espp` integration first and reuse it consistently.

---

## Error Handling

Implement appropriate handling for:

* SPIFFS not mounted.
* Network unavailable.
* FTP initialization failure.
* FTP client connection/disconnection.
* Invalid filesystem paths.
* File-not-found conditions.
* Permission/access failures.
* Upload/download failures.
* Invalid FTP operations.
* Filesystem capacity/full conditions.

FTP failures must not crash or restart the main application.

---

## Lifecycle

Define a clean lifecycle for the FTP server:

```text
Application startup
      │
      ▼
SPIFFS initialized/mounted
      │
      ▼
Network available
      │
      ▼
FTP server initialized
      │
      ▼
FTP server running
      │
      ├── FileZilla connects
      ├── Browse filesystem
      ├── Upload/download
      └── File operations
```

Ensure shutdown/deinitialization is handled appropriately if the application architecture supports it.

---

## Testing

After implementation, verify at minimum:

### Build

* The project builds successfully.
* Existing build configurations still build.
* No unrelated compiler warnings/errors are introduced.

### SPIFFS

Verify that:

* Existing SPIFFS initialization still works.
* Existing application files remain accessible.
* Files created through FTP are visible to the application.
* Files created by the application are visible through FTP.

### FTP

Test:

* Server startup.
* Client connection.
* Authentication.
* Directory listing.
* Directory creation.
* Directory deletion.
* File upload.
* File download.
* File deletion.
* File rename.
* File overwrite.
* Large-enough file transfers to exercise real filesystem I/O.
* Multiple FTP operations in sequence.
* Client disconnect/reconnect.

### FileZilla

Explicitly test with FileZilla:

```text
Connect
  → Login
  → Browse root
  → Create directory
  → Upload file
  → Verify file
  → Download file
  → Delete file
  → Delete directory
  → Disconnect
  → Reconnect
```

Confirm that transferred files are actually stored in the existing SPIFFS partition.

---

## Constraints

**Do not:**

* Change partition sizes/layouts.
* Add a new filesystem partition.
* Replace SPIFFS.
* Rewrite unrelated application code.
* Change existing system behavior.
* Break existing configuration.
* Duplicate the `espp/ftp` implementation.
* Implement a completely separate FTP protocol stack.
* Introduce unnecessary abstractions.
* Make invasive architectural changes.

**Prefer:**

* Minimal changes.
* Existing project patterns.
* Existing SPIFFS APIs.
* Existing `espp/ftp` APIs.
* Existing network infrastructure.
* Existing configuration mechanisms.
* Clean separation of the new `ftp_server` feature.

---

## Expected Deliverables

Provide:

1. The complete implementation of the new `ftp_server` feature.
2. Required component/dependency changes.
3. Required Kconfig/configuration changes, if applicable.
4. Integration with the existing application lifecycle.
5. No partition/configuration regressions.
6. Verification that SPIFFS remains unchanged.
7. Verification that FileZilla can browse and transfer files.
8. A concise summary of:

   * Files changed.
   * Why each file was changed.
   * Configuration changes.
   * How FTP is initialized.
   * How FTP maps to SPIFFS.
   * How to enable/configure the feature.
   * How to connect using FileZilla.
   * Any limitations imposed by SPIFFS or `espp/ftp`.

## Implementation Approach

Before modifying code, inspect the repository and determine the **smallest, most idiomatic change set** required.

Do not assume the existing architecture. Base the implementation on the actual `feat/knx_ip` branch and the versions/APIs of `espp/ftp` available in that repository.

At the end, report any assumptions, compatibility limitations, or issues discovered during implementation.
