# FTP Server

The optional FTP server exposes an already-mounted ESP VFS filesystem through
the managed `espp/ftp` component. It starts after XiaoZhi receives a network
connected event and stops on network disconnect. FTP sessions run in the tasks
owned by `espp/ftp`, outside the main application and audio tasks.

## Configuration

Open `Xiaozhi Assistant -> FTP Server Configuration` in menuconfig.

| Option | Default | Purpose |
| --- | --- | --- |
| `CONFIG_XIAOZHI_FTP_SERVER` | off | Compile and enable the FTP server |
| `CONFIG_XIAOZHI_FTP_SERVER_PORT` | `21` | FTP control port |
| `CONFIG_XIAOZHI_FTP_SERVER_ROOT` | `assets` | Root of the existing assets VFS filesystem |

The configured `assets` root must exist as a VFS directory before the
network-connected event. The FTP feature deliberately does not mount, format,
or unmount storage. A missing mount is logged and leaves the rest of the
application running normally. The selected board is responsible for exposing
the path without overwriting the packed assets partition used by the
application. No partition table is changed by this feature.

## FileZilla

1. Enable the FTP server and ensure the selected board mounts the configured
   filesystem before starting its network connection.
2. Read the device IPv4 address from the `FTP_SERVER` startup log.
3. In FileZilla, use plain FTP, the configured port, passive transfer mode, and
   any non-empty username and password.
4. Connect and use normal listing, upload, download, overwrite, delete, and
   rename operations.

Passive data sockets use ports selected by `espp/ftp` from 1024 through 11023.
The client must be able to reach those ports on the device network.

## Security And Filesystem Limits

`espp/ftp` 1.x accepts every username and password and provides unencrypted
FTP. Enable the server only on a trusted, isolated network and disable it in
production builds that do not need file transfer.

The upstream 1.x command handlers also do not enforce the configured root as a
security sandbox against absolute paths or parent-directory traversal. Treat
the server as having access to the device's mounted VFS paths, not only to the
configured starting directory.

Available operations ultimately depend on the mounted filesystem. In
particular, ESP-IDF SPIFFS stores a flat namespace and does not provide real
directories, so directory creation, removal, and navigation may fail even
though the FTP protocol component implements `CWD`, `MKD`, and `RMD`. File
upload, download, overwrite, delete, and rename are also subject to normal VFS
errors such as a full filesystem, invalid path, or missing file.

The upstream component supports `USER`, `PASS`, `SYST`, `FEAT`, `PWD`, `CWD`,
`CDUP`, `TYPE`, `PASV`, `PORT`, `LIST`, `SIZE`, `RETR`, `STOR`, `DELE`, `MKD`,
`RMD`, `RNFR`, `RNTO`, `NOOP`, and `QUIT`. It does not advertise FTPS, `MLSD`,
or `EPSV`; configure FileZilla to fall back to plain FTP with `LIST` and `PASV`.