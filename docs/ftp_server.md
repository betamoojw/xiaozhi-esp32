# FTP Server

The optional FTP server exposes the writable LittleFS filesystem mounted from
partition `lfs` at `/littlefs`. It starts after XiaoZhi receives a network
connected event and stops on network disconnect. FTP sessions run in tasks
owned by the project-patched `espp/ftp` component, outside the main application
and audio tasks.

## Configuration

Open `Xiaozhi Assistant -> FTP Server Configuration` in menuconfig.

| Option | Default | Purpose |
| --- | --- | --- |
| `CONFIG_XIAOZHI_FTP_SERVER` | on | Compile and enable the FTP server |
| `CONFIG_XIAOZHI_FTP_SERVER_PORT` | `21` | FTP control port |
| `CONFIG_XIAOZHI_FTP_SERVER_ROOT` | `/littlefs` | Root inside the LittleFS VFS mount |

LittleFS is mounted before networking starts. FTP refuses to start if the mount
is unavailable, if the configured root does not exist, or if the root is not
`/littlefs` or one of its subdirectories. The packed `assets` partition is not
mounted or exposed through FTP.

## FileZilla

1. Use a flash layout containing the `lfs` partition and enable the FTP server.
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

The project pins and locally overrides `espp/ftp` 1.3.1 to normalize every
path-bearing command. Absolute FTP paths are interpreted relative to the FTP
root, parent-directory traversal is rejected, and the root itself cannot be
removed or renamed. This applies to `CWD`, `CDUP`, `SIZE`, `RETR`, `STOR`,
`DELE`, `MKD`, `RMD`, `RNFR`, and `RNTO`.

Available operations ultimately depend on LittleFS and are subject to normal
VFS errors such as a full filesystem, invalid path, or missing file.

The upstream component supports `USER`, `PASS`, `SYST`, `FEAT`, `PWD`, `CWD`,
`CDUP`, `TYPE`, `PASV`, `PORT`, `LIST`, `SIZE`, `RETR`, `STOR`, `DELE`, `MKD`,
`RMD`, `RNFR`, `RNTO`, `NOOP`, and `QUIT`. It does not advertise FTPS, `MLSD`,
or `EPSV`; configure FileZilla to fall back to plain FTP with `LIST` and `PASV`.