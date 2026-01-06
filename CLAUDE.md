# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

uFTP is a lightweight FTP server written in C, designed for servers and embedded ARM devices. It supports TLS/SSL encryption, IPv6, PAM authentication, and large file transfers. The server is multi-threaded, handling multiple concurrent client connections.

## Build System

The project uses a Makefile-based build system with extensive configuration options.

### Building

```bash
# Standard build
make

# Clean build artifacts
make clean
```

### Build Configuration

Key Makefile flags that can be enabled by uncommenting the relevant lines:

- `ENABLE_LARGE_FILE_SUPPORT`: Support for files >2GB (recommended)
- `ENABLE_OPENSSL_SUPPORT`: Enable TLS/SSL support (requires `-lssl -lcrypto`)
- `ENABLE_IPV6_SUPPORT`: Enable IPv6 connections
- `ENABLE_PAM_SUPPORT`: Enable PAM-based authentication (requires `-lpam`)
- `ENABLE_PRINTF`: Enable debug printf statements
- `ENDFLAG=-static`: Compile as static binary (useful for embedded systems)

The Makefile supports cross-compilation (see commented `CC` lines for musl-gcc and ARM cross-compilers).

### Output

Compiled binary: `./build/uFTP`

Object files: `./build/modules/*.o`

## Testing

Python-based integration tests are located in `test/`:

```bash
# Run integration tests (requires server running)
python test/integration.py
```

Individual test files (`test1.py` through `test8.py`) test specific FTP features.

## Architecture

### High-Level Structure

The server follows a multi-threaded architecture:

1. **Main thread** (`uFTP.c` → `ftpServer.c`): Initializes server, binds socket, enters main event loop
2. **Control channel loop** (`controlChannel/controlChannel.c`): Uses `select()` to monitor client sockets, dispatches FTP commands
3. **Data channel workers** (`dataChannel/dataChannel.c`): Spawned threads handle data transfers (RETR, STOR, LIST) separately from control channel

### Key Data Structures

**`ftpDataType`** (in `ftpData.h`): Central server state containing:
- `ftpParameters`: Server configuration read from config file
- `clients[]`: Array of client connection states
- `connectionData`: Socket descriptors and fd_sets for select()
- `loginFailsVector`: Tracks failed login attempts for anti-bruteforce

**`clientDataType`**: Per-client state including:
- Socket descriptor and buffers
- Login credentials and authentication state
- Current working directory and FTP path
- SSL/TLS state (if enabled)
- `workerData`: State for active data channel thread
- Memory table for dynamic allocations

### Control Flow

1. Main loop calls `evaluateControlChannel()` which performs `select()` on all client sockets
2. When a client socket has data, command is read into buffer
3. Command is parsed and dispatched via command handler map in `controlChannel.c`
4. Command handlers are implemented in `ftpCommandElaborate.c`
5. Data transfer commands spawn worker threads via `connectionWorkerHandle()` in `dataChannel/dataChannel.c`

### Module Organization

- **Root directory**: FTP protocol logic (`ftpCommandElaborate.c`, `ftpData.c`, `ftpServer.c`)
- **`library/`**: Reusable utilities (file management, config parsing, SSL, authentication, logging, dynamic memory)
- **`controlChannel/`**: Control channel event loop and command dispatching
- **`dataChannel/`**: Data transfer worker threads
- **`test/`**: Python integration tests

### Memory Management

The server uses a custom memory tracking system (`library/dynamicMemory.c`):
- Each client and subsystem has a `DYNMEM_MemoryTable_DataType*`
- Allocations are tracked for proper cleanup on client disconnect
- Use `DYNMEM_malloc()` and `DYNMEM_freeAll()` instead of raw malloc/free

### Configuration

Server reads configuration from `/etc/uftpd.cfg` (or path specified). See `uftpd.cfg` for sample with:
- Port, max connections, timeouts
- TLS certificate paths
- User accounts with home directories and ownership settings
- PAM authentication toggle
- Passive port range for data connections
- NAT IP configuration

### Security Features

- **Anti-bruteforce**: `MAX_CONNECTION_TRY_PER_IP` limits failed logins per IP
- **TLS/SSL**: Optional FTPS support with `FORCE_TLS` mode
- **PAM integration**: System account authentication
- **Ownership control**: Set file ownership per user via `GROUP_NAME_OWNER` and `USER_NAME_OWNER`
- **Connection limits**: Per-IP connection limits and idle timeouts

### IPv6 Support

When `IPV6_ENABLED` is defined:
- Socket structures use `sockaddr_in6` instead of `sockaddr_in`
- Both IPv4 and IPv6 clients supported
- Extended PASV mode (EPSV) available for IPv6 data connections

### Debugging

Uncomment `ENABLE_PRINTF` in Makefile to enable debug output via `my_printf()` macro throughout the codebase.

---

## Cross-Compilation for Embedded ARM Devices

uFTP has been tested and deployed on embedded ARM devices (DVR/NVR systems). Build scripts are provided for cross-compilation.

### ARM64 (aarch64) Build

For ARM Cortex-A53 and similar 64-bit ARM processors:

```bash
sh build_aarch64.sh          # Release build
sh build_aarch64.sh debug    # Debug build
```

**Compiler:** `/opt/aarch64-ca53-linux-gnueabihf-8.4.01/bin/aarch64-ca53-linux-gnu-gcc`

**Output:**
- Binary: `./build/uFTP_aarch64_package/bin/uFTP` (~762KB stripped)
- Package: `./build/uFTP_aarch64_release.tar.gz`

### ARM32 (armv7) Build

For ARM Cortex-A9 and similar 32-bit ARM processors:

```bash
sh build_arm32.sh            # Release build
sh build_arm32.sh debug      # Debug build
```

**Compiler:** `/opt/arm-ca9-linux-gnueabihf-6.5/bin/arm-ca9-linux-gnueabihf-gcc`

**Output:**
- Binary: `./build/uFTP_arm32_package/bin/uFTP` (~678KB stripped)
- Package: `./build/uFTP_arm32_release.tar.gz`

### Deployment on Target Device

```bash
# Extract package
tar -xzf uFTP_arm*.tar.gz

# Install binary (adjust path based on target filesystem)
cp uFTP_*/bin/uFTP /usr/bin/uFTP
chmod +x /usr/bin/uFTP

# Copy config
cp uFTP_*/etc/uftpd.cfg.sample /etc/uftpd.cfg
vi /etc/uftpd.cfg  # Configure as needed

# Run
/usr/bin/uFTP
```

---

## Known Issues and Fixes

### Issue 1: IPv6 Socket Creation Failure

**Symptom:**
```
socket() failed: Address family not supported by protocol
```

**Cause:** System kernel doesn't support IPv6, but uFTP was compiled with `ENABLE_IPV6_SUPPORT`.

**Fix:**
IPv6 support is now **disabled by default** in Makefile and build scripts. To re-enable:
```makefile
# In Makefile or build script
ENABLE_IPV6_SUPPORT=-D IPV6_ENABLED
```

### Issue 2: Segmentation Fault in Static Builds

**Symptom:**
```
potentially unexpected fatal signal 11
Crash during LIST command after "total XX"
```

**Cause:**
`getpwuid_r()` and `getgrgid_r()` crash in statically-linked binaries on embedded systems due to NSS library dependencies.

**Fix (APPLIED):**
Modified `library/fileManagement.c`:
- `FILE_GetOwner()`: Returns numeric UID instead of username
- `FILE_GetGroupOwner()`: Returns numeric GID instead of group name

**Example output:**
```
-rwxr-xr-x 1 0 0 2490444 Jan 06 2026 SYSTEM.log
              ^ ^
            UID GID (numeric)
```

This is expected behavior in static builds. The numeric IDs are functional and don't cause crashes.

---

## Production Deployment Examples

### Read-Only FTP for Log Files

**Use case:** Expose only specific log files via FTP

**Setup:**
```bash
# Create FTP public directory
mkdir -p /var/ftp_public
chmod 755 /var/ftp_public

# Symlink log file
ln -s /dvr/SYSTEM.log /var/ftp_public/SYSTEM.log

# Configure user in /etc/uftpd.cfg
USER_0 = admin
PASSWORD_0 = <secure-password>
HOME_0 = /var/ftp_public
```

**Result:** Users only see SYSTEM.log when connecting.

### Security Best Practices

1. **Use strong passwords** in `/etc/uftpd.cfg`
2. **Disable unnecessary users** (remove sample USER_1, USER_2)
3. **Set appropriate permissions** on FTP directories (755 for directories, 644 for files)
4. **Use FTPS** (FTP over SSL/TLS) for encrypted transfers:
   ```bash
   # Generate certificates
   mkdir -p /etc/uFTP
   openssl req -x509 -newkey rsa:2048 -keyout /etc/uFTP/key.pem \
       -out /etc/uFTP/cert.pem -days 365 -nodes -subj "/CN=FTP-Server"

   # Enable in uftpd.cfg
   CERTIFICATE_PATH=/etc/uFTP/cert.pem
   PRIVATE_CERTIFICATE_PATH=/etc/uFTP/key.pem
   FORCE_TLS = true
   ```

---

## Protocol Support

### Supported: FTPS (FTP over SSL/TLS)

uFTP supports **FTPS** when compiled with OpenSSL:
- Protocol: FTP with SSL/TLS encryption
- Ports: 21 (control), 10000-50000 (passive data)
- Standard: RFC 4217
- Clients: FileZilla, WinSCP, Windows FTP, command-line ftp

### NOT Supported: SFTP (SSH File Transfer Protocol)

uFTP does **NOT** support **SFTP**:
- SFTP uses SSH protocol (port 22)
- Completely different from FTP/FTPS
- Requires OpenSSH server or Dropbear
- Not compatible with FTP protocol

**If SFTP is required:** Use OpenSSH server or Dropbear instead of uFTP.

---

## Troubleshooting

### Server starts but no connections accepted

**Check if listening on port:**
```bash
netstat -tlnp | grep :21
```

If nothing appears, check:
1. IPv6 disabled (if kernel doesn't support it)
2. Configuration file syntax (`/etc/uftpd.cfg`)
3. Firewall rules

### Connection refused errors

**Common causes:**
1. Server not running: `ps aux | grep uFTP`
2. Wrong port (default is 21)
3. Firewall blocking connections
4. Binary architecture mismatch (ARM32 vs ARM64)

**Verify binary architecture:**
```bash
file /usr/bin/uFTP
# Should show: "ARM aarch64" or "ARM, EABI5"
```

### Directory listing crashes

If server crashes during `ls` command:
- Likely using old version with `getpwuid_r` bug
- Rebuild with latest code (fix applied to `library/fileManagement.c`)
- Check `dmesg` for segfault messages

### Testing FTP Functionality

**Clients tested:**
- ✅ Windows built-in FTP client
- ✅ Linux command-line ftp
- ✅ FileZilla (recommended for GUI)
- ✅ WinSCP (FTPS mode)

**Test sequence:**
```bash
ftp <server-ip>
# Login with credentials from uftpd.cfg
ls          # List files
get <file>  # Download file
bye         # Disconnect
```

---

## Configuration Examples

### Minimal Configuration

```conf
# /etc/uftpd.cfg
MAXIMUM_ALLOWED_FTP_CONNECTION = 10
FTP_PORT = 21
DAEMON_MODE = true
LOG_FOLDER = /var/log/
MAXIMUM_LOG_FILES = 0

# Single user
USER_0 = admin
PASSWORD_0 = admin123
HOME_0 = /var/ftp_public
```

### Multi-User with Isolated Directories

```conf
# Admin user - full access
USER_0 = admin
PASSWORD_0 = <strong-password>
HOME_0 = /

# Read-only logs user
USER_1 = logviewer
PASSWORD_1 = <password>
HOME_1 = /var/ftp_public

# Web content user
USER_2 = webmaster
PASSWORD_2 = <password>
HOME_2 = /var/www/html
GROUP_NAME_OWNER_2 = www-data
USER_NAME_OWNER_2 = www-data
```

---

## Build Customization

### Compiler Paths

If using different cross-compilers, edit build scripts:

**For ARM64:**
```bash
# In build_aarch64.sh
CROSS_COMPILER="/path/to/your/aarch64-gcc"
CROSS_PREFIX="/path/to/your/aarch64-"
```

**For ARM32:**
```bash
# In build_arm32.sh
CROSS_COMPILER="/path/to/your/arm-gcc"
CROSS_PREFIX="/path/to/your/arm-"
```

### Static vs Dynamic Linking

**Static (default, recommended for embedded):**
```bash
ENDFLAG="-static"
```
- Self-contained binary
- No library dependencies
- Larger size (~700KB-800KB)

**Dynamic:**
```bash
ENDFLAG=""
```
- Smaller binary
- Requires shared libraries on target
- May have compatibility issues
