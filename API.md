# FluxDrop Core Engine API

The FluxDrop engine is exposed as an **ABI-stable C interface** defined in [`Engine/include/fluxdrop_core.h`](Engine/include/fluxdrop_core.h).

The engine compiles into a pre-built dynamic shared library (**`libfluxdrop_core.so`** on Linux, **`fluxdrop_core.dll`** on Windows) as well as a static library (**`libfluxdrop_core.a`**).

> [!IMPORTANT]
> **No Engine Source Copying Required!**
> You do **not** need to copy any internal Engine C++ source files (`networking.cpp`, `transfer.cpp`, `session.cpp`, etc.) or install internal C++ dependencies (Boost.Asio, libsodium, nlohmann-json) in your project.
> All internal dependencies and networking mechanics are encapsulated inside `libfluxdrop_core.so` / `fluxdrop_core.dll`. To integrate FluxDrop into any external project (C, C++, Python, Flutter/Dart, Rust, Go, C#), you only need:
> 1. The header file: `fluxdrop_core.h`
> 2. The compiled shared library: `libfluxdrop_core.so` (or `fluxdrop_core.dll` / `libfluxdrop_core.a`)

All functions are thread-safe and non-blocking for UI threads: transfers execute on background threads, and status/progress is reported asynchronously via callbacks.

---

## Table of Contents

1. [Quick Start (Bidirectional Session)](#quick-start-bidirectional-session)
2. [Data Types](#data-types)
3. [Engine Lifecycle](#engine-lifecycle)
4. [Modern Session API (Bidirectional Peer-to-Peer)](#modern-session-api-bidirectional-peer-to-peer)
5. [Device Discovery API](#device-discovery-api)
6. [Legacy Unidirectional API](#legacy-unidirectional-api)
7. [Callbacks Reference](#callbacks-reference)
8. [Protocol & Wire Format](#protocol--wire-format)
9. [Integration Guide (Using the Compiled Library)](#integration-guide-using-the-compiled-library)
   - [A. C / C++ (Dynamic Linking with CMake)](#a-c--c-dynamic-linking-with-cmake)
   - [B. C / C++ (GCC / Clang Command Line)](#b-c--c-gcc--clang-command-line)
   - [C. Python (ctypes FFI — Zero Compiler Setup)](#c-python-ctypes-ffi--zero-compiler-setup)
   - [D. Dart / Flutter FFI](#d-dart--flutter-ffi)
   - [E. Static Linking](#e-static-linking)

---

## Quick Start (Bidirectional Session)

### Host Example

```c
#include "fluxdrop_core.h"
#include <stdio.h>

void on_ready(const char* ip, int port, int pin) {
    printf("Hosting session on %s:%d | PIN: %d\n", ip, port, pin);
}

void on_established(const fd_session_info_t* info) {
    printf("Connected with guest at %s:%d!\n", info->peer_ip, info->peer_port);
    // Either peer can send files at any time:
    const char* files[] = {"photo.jpg", "document.pdf"};
    fd_session_send_files(files, 2);
}

bool on_file_offer(const char* filename, uint64_t file_size) {
    printf("Incoming file offer: %s (%lu bytes). Auto-accepting.\n", filename, file_size);
    return true; // Return true to accept, false to reject
}

void on_progress(const char* filename, uint64_t transferred, uint64_t total, double speed_mbps) {
    int pct = total > 0 ? (int)((transferred * 100) / total) : 0;
    printf("\rTransferring %s: %d%% (%.2f MB/s)", filename, pct, speed_mbps);
    fflush(stdout);
}

int main() {
    fd_init();
    fd_session_set_save_dir("./downloads");

    fd_session_host(on_ready, on_established, NULL, NULL, NULL, on_file_offer, on_progress, NULL);

    // Keep running until user terminates
    getchar();

    fd_session_disconnect();
    fd_cleanup();
    return 0;
}
```

### Guest Example

```c
#include "fluxdrop_core.h"
#include <stdio.h>

void on_established(const fd_session_info_t* info) {
    printf("Connected to host at %s:%d!\n", info->peer_ip, info->peer_port);
}

int main() {
    fd_init();

    fd_session_join("192.168.1.50", 49152, "1234", "./downloads",
                    on_established, NULL, NULL, NULL, NULL, NULL, NULL);

    getchar();

    fd_session_disconnect();
    fd_cleanup();
    return 0;
}
```

---

## Data Types

### `fd_device_t`
Represents a host device discovered over the local network via UDP broadcast/multicast.

```c
typedef struct {
    uint32_t session_id; // Unique room or session identifier
    int port;            // Host TCP listening port
    const char* ip;      // Host IPv4 address (e.g. "192.168.1.10")
} fd_device_t;
```

### `fd_session_info_t`
Metadata provided when a bidirectional peer-to-peer session is established.

```c
typedef struct {
    uint32_t session_id; // Authenticated session ID
    const char* peer_ip; // Remote peer IPv4 address
    int peer_port;       // Remote peer port
    int role;            // 0 = HOST, 1 = GUEST
} fd_session_info_t;
```

---

## Engine Lifecycle

| Function | Signature | Description |
|---|---|---|
| `fd_init` | `void fd_init(void);` | Initializes the engine, initializes libsodium crypto, and prepares networking subsystems. Call once at application launch. |
| `fd_cleanup` | `void fd_cleanup(void);` | Gracefully closes active sessions, aborts transfers, joins worker threads, and releases resources. Call before exit. |

---

## Modern Session API (Bidirectional Peer-to-Peer)

Once a session is established between Host and Guest, connection symmetry is achieved: **either peer can send files to the other at any time over the persistent authenticated TCP link**.

| Function | Signature | Description |
|---|---|---|
| `fd_session_host` | `void fd_session_host(ready_cb, established_cb, ended_cb, status_cb, error_cb, file_offer_cb, progress_cb, file_complete_cb);` | Starts hosting a session. Binds an ephemeral TCP port, generates a 4-digit PIN, starts UDP discovery broadcasting, and listens for a guest. |
| `fd_session_join` | `void fd_session_join(ip, port, pin, save_dir, established_cb, ended_cb, status_cb, error_cb, file_offer_cb, progress_cb, file_complete_cb);` | Connects to an existing host at `ip:port`, authenticates using the 4-digit `pin`, and configures `save_dir`. |
| `fd_session_send_files` | `void fd_session_send_files(const char** file_paths, int num_files);` | Enqueues one or more files or directories to be sent to the connected peer over the active session. Directories are traversed recursively preserving folder structure. |
| `fd_session_send_files_with_names` | `void fd_session_send_files_with_names(const char** file_paths, const char** file_names, int num_files);` | Enqueues files with custom wire display names (useful for mobile content URIs or virtual file wrappers). |
| `fd_session_set_save_dir` | `void fd_session_set_save_dir(const char* dir);` | Dynamically updates the destination directory for incoming files. |
| `fd_session_disconnect` | `void fd_session_disconnect(void);` | Gracefully terminates the active session or stops waiting for guests. Notifies the remote peer with `SESSION_END`. |
| `fd_session_get_pin` | `int fd_session_get_pin(void);` | Returns the active 4-digit host PIN (or 0 if not hosting). |
| `fd_session_get_port` | `int fd_session_get_port(void);` | Returns the active listening TCP port (or 0). |
| `fd_session_get_ip` | `const char* fd_session_get_ip(void);` | Returns the bound local IP address string. |
| `fd_session_is_connected` | `bool fd_session_is_connected(void);` | Returns `true` if a bidirectional session is currently established and active. |

---

## Device Discovery API

FluxDrop uses dual-layer UDP discovery (Broadcast `255.255.255.255:45454` and Multicast `239.255.45.45:45454`) to automatically find nearby hosts across WiFi and hotspot environments.

| Function | Signature | Description |
|---|---|---|
| `fd_start_discovery` | `void fd_start_discovery(uint32_t room_id, fd_client_device_found_cb found_cb);` | Starts listening for nearby hosts. Calls `found_cb` each time a valid device advertisement is received. Pass `482913` for standard default room. |
| `fd_stop_discovery` | `void fd_stop_discovery(void);` | Stops the background UDP discovery listener thread. |

---

## Legacy Unidirectional API

The unidirectional API remains available for simple single-drop command-line tools where a server only sends and a client only receives.

| Function | Signature | Description |
|---|---|---|
| `fd_start_server` | `void fd_start_server(paths, count, ready_cb, status_cb, error_cb, progress_cb, complete_cb);` | Starts a one-shot send server with pre-selected files. |
| `fd_cancel_server` | `void fd_cancel_server(void);` | Blocking cancellation of the send server. |
| `fd_request_cancel_server` | `void fd_request_cancel_server(void);` | Non-blocking cancellation request. |
| `fd_connect` | `void fd_connect(ip, port, pin, save_dir, status_cb, error_cb, file_request_cb, progress_cb, complete_cb);` | Connects as a one-shot receiver client. |
| `fd_cancel_client` | `void fd_cancel_client(void);` | Blocking cancellation of the receive client. |
| `fd_request_cancel_client` | `void fd_request_cancel_client(void);` | Non-blocking cancellation request. |

---

## Callbacks Reference

### Session Callbacks

```c
// Invoked when host is ready and listening
typedef void (*fd_session_ready_cb)(const char* ip, int port, int pin);

// Invoked when mutual PIN authentication succeeds
typedef void (*fd_session_established_cb)(const fd_session_info_t* info);

// Invoked when the session terminates or peer disconnects
typedef void (*fd_session_ended_cb)();

// Informational status message for UI or logs
typedef void (*fd_session_status_cb)(const char* message);

// Error notification callback
typedef void (*fd_session_error_cb)(const char* error);

// Invoked when remote peer offers an incoming file.
// Return true to accept the file, false to reject.
typedef bool (*fd_session_file_offer_cb)(const char* filename, uint64_t file_size);

// Real-time transfer progress callback (filename, bytes transferred, total bytes, speed in MB/s)
typedef void (*fd_session_progress_cb)(const char* filename, uint64_t transferred, uint64_t total, double speed_mbps);

// Invoked when an individual file transfer completes successfully
typedef void (*fd_session_file_complete_cb)(const char* filename);
```

### Discovery & Client Callbacks

```c
typedef void (*fd_client_device_found_cb)(const fd_device_t* device);
typedef void (*fd_client_status_cb)(const char* message);
typedef void (*fd_client_error_cb)(const char* error);
typedef bool (*fd_client_file_request_cb)(const char* filename, uint64_t file_size);
typedef void (*fd_client_progress_cb)(const char* filename, uint64_t transferred, uint64_t total, double speed_mbps);
typedef void (*fd_client_complete_cb)();
```

---

## Protocol & Wire Format

Each packet sent over TCP starts with a 16-byte binary header formatted in big-endian network byte order:

| Field | Size | Description |
|---|---|---|
| `command` | 4 bytes | Command type ID (network order `htonl`) |
| `payload_size` | 4 bytes | Byte length of payload following this header |
| `session_id` | 4 bytes | Room / session identifier |
| `reserved` | 4 bytes | Reserved / flags (e.g. chunk index) |

### Core Command IDs

| Command | Value | Description |
|---|---|---|
| `FILE_META` | `1` | JSON metadata containing filename, file size, relative path, and Blake2b hash |
| `FILE_CHUNK`| `2` | 64KB binary chunk payload |
| `CANCEL`    | `3` | Transfer cancellation request |
| `PING`      | `4` | Keepalive heartbeat ping |
| `PONG`      | `5` | Keepalive heartbeat reply |
| `RESUME`    | `6` | Resume offset request for `.fluxpart` files |
| `AUTH`      | `7` | PIN authentication payload |
| `AUTH_OK`   | `8` | Authentication accepted |
| `AUTH_FAIL` | `9` | Authentication rejected (incorrect PIN) |
| `SESSION_END`| `10` | Session disconnect signal |

---

## Integration Guide (Using the Compiled Library)

Building external applications does **not** require copying Engine source files or installing Boost. You simply link against **`libfluxdrop_core.so`** (Linux) or **`fluxdrop_core.dll`** (Windows).

### A. C / C++ (Dynamic Linking with CMake)

In your consuming project's `CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.16)
project(MyFluxDropApp C)

# 1. Point to the directory containing fluxdrop_core.h
include_directories(/path/to/FluxDrop/Engine/include)

# 2. Add your executable
add_executable(my_app src/main.c)

# 3. Link directly against the shared library
target_link_libraries(my_app PRIVATE
    /path/to/FluxDrop/Engine/build/libfluxdrop_core.so
)

# 4. Set rpath so the executable finds libfluxdrop_core.so at runtime
set_target_properties(my_app PROPERTIES
    BUILD_RPATH "/path/to/FluxDrop/Engine/build"
    INSTALL_RPATH "$ORIGIN/lib"
)
```

### B. C / C++ (GCC / Clang Command Line)

Compile directly with GCC or Clang:

```bash
# Compile and link against libfluxdrop_core.so
gcc main.c \
    -I/path/to/FluxDrop/Engine/include \
    -L/path/to/FluxDrop/Engine/build \
    -lfluxdrop_core \
    -Wl,-rpath,/path/to/FluxDrop/Engine/build \
    -o my_app

# Run application
./my_app
```

### C. Python (`ctypes` FFI — Zero Compiler Setup)

Because `fluxdrop_core.h` is a standard C interface, you can call it directly from Python with zero compilation:

```python
import ctypes
import os
import time

# Load the shared library
lib_path = os.path.abspath("Engine/build/libfluxdrop_core.so")
fluxdrop = ctypes.CDLL(lib_path)

# Define callback types
READY_CB = ctypes.CFUNCTYPE(None, ctypes.c_char_p, ctypes.c_int, ctypes.c_int)
ESTABLISHED_CB = ctypes.CFUNCTYPE(None, ctypes.c_void_p)
OFFER_CB = ctypes.CFUNCTYPE(ctypes.c_bool, ctypes.c_char_p, ctypes.c_uint64)

@READY_CB
def on_ready(ip, port, pin):
    print(f"[Python] Host ready on {ip.decode()}:{port} — PIN: {pin}")

@ESTABLISHED_CB
def on_established(info):
    print("[Python] Peer connected successfully!")

@OFFER_CB
def on_file_offer(filename, size):
    print(f"[Python] Incoming file: {filename.decode()} ({size} bytes) -> Auto-accepting")
    return True

# Initialize engine
fluxdrop.fd_init()
fluxdrop.fd_session_set_save_dir(b"./downloads")

# Start hosting
fluxdrop.fd_session_host(
    on_ready,
    on_established,
    None, None, None,
    on_file_offer,
    None, None
)

print("Hosting session... Press Ctrl+C to stop.")
try:
    while True:
        time.sleep(1)
except KeyboardInterrupt:
    fluxdrop.fd_session_disconnect()
    fluxdrop.fd_cleanup()
    print("Cleaned up and exited.")
```

### D. Dart / Flutter FFI

In Flutter or standalone Dart:

```dart
import 'dart:ffi';
import 'package:ffi/ffi.dart';

typedef FdInitC = Void Function();
typedef FdInitDart = void Function();

class FluxDrop {
  late DynamicLibrary _dylib;
  late FdInitDart init;

  FluxDrop() {
    // Open the prebuilt shared object
    _dylib = DynamicLibrary.open("libfluxdrop_core.so");
    init = _dylib.lookupFunction<FdInitC, FdInitDart>("fd_init");
  }
}
```

### E. Static Linking

If you prefer a single monolithic binary without shipping `.so`/`.dll` files:
1. Link against `libfluxdrop_core.a`
2. Link the core's static dependencies: `Threads::Threads`, `libsodium`, `Boost`, and platform networking libraries (`ws2_32` on Windows).
