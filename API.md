# FluxDrop Core Engine API

The engine is exposed as a **C API** (`fluxdrop_core.h`) so any frontend (CLI, GUI, mobile, or your own project) can use it. All functions are thread-safe: transfers run on background threads, and progress is reported via callbacks.

---

## Quick Start

```c
#include "fluxdrop_core.h"

// 1. Initialize
fd_init();

// 2. Send files
const char* files[] = {"photo.jpg", "document.pdf"};
fd_start_server(files, 2,
    on_ready,       // called with IP, port, PIN
    on_status,      // status messages
    on_error,       // error messages
    on_progress,    // filename, transferred, total, speed_mbps
    on_complete     // all files sent
);

// 3. Cancel (non-blocking)
fd_request_cancel_server();

// 4. Cleanup
fd_cleanup();
```

---

## Types

```c
typedef struct {
    uint32_t session_id;
    int port;
    const char* ip;
} fd_device_t;
```

---

## Lifecycle

| Function | Description |
|----------|-------------|
| `fd_init()` | Initialize the engine (call once at startup). |
| `fd_cleanup()` | Stop all transfers, join threads, free resources. |

---

## Server (Sender) Functions

| Function | Description |
|----------|-------------|
| `fd_start_server(paths, count, ready_cb, status_cb, error_cb, progress_cb, complete_cb)` | Start sharing files. Spawns a background thread, broadcasts for discovery, waits for a receiver to connect. |
| `fd_cancel_server()` | **Blocking** cancel — stops the server, joins the thread, resets state. |
| `fd_request_cancel_server()` | **Non-blocking** cancel — signals stop, thread exits on its own. |

---

## Client (Receiver) Functions

| Function | Description |
|----------|-------------|
| `fd_start_discovery(room_id, found_cb)` | Start listening for sender broadcasts. Calls `found_cb` for each discovered device. |
| `fd_stop_discovery()` | Stop listening for broadcasts. |
| `fd_connect(ip, port, pin, save_dir, status_cb, error_cb, file_request_cb, progress_cb, complete_cb)` | Connect to a sender at `ip:port`, authenticate with `pin`, receive files to `save_dir`. |
| `fd_cancel_client()` | **Blocking** cancel. |
| `fd_request_cancel_client()` | **Non-blocking** cancel. |

---

## Callbacks

```c
// Server
typedef void (*fd_server_ready_cb)(const char* ip, int port, int pin);
typedef void (*fd_server_status_cb)(const char* message);
typedef void (*fd_server_error_cb)(const char* error);
typedef void (*fd_server_progress_cb)(const char* filename, uint64_t transferred,
                                      uint64_t total, double speed_mbps);
typedef void (*fd_server_complete_cb)();

// Client
typedef void (*fd_client_device_found_cb)(const fd_device_t* device);
typedef void (*fd_client_status_cb)(const char* message);
typedef void (*fd_client_error_cb)(const char* error);
typedef bool (*fd_client_file_request_cb)(const char* filename, uint64_t file_size);
typedef void (*fd_client_progress_cb)(const char* filename, uint64_t transferred,
                                      uint64_t total, double speed_mbps);
typedef void (*fd_client_complete_cb)();
```

---

## Protocol Packet Format

| Field          | Size    | Description                   |
|----------------|---------|-------------------------------|
| `command`      | 4 bytes | Command type (network order)  |
| `payload_size` | 4 bytes | Size of following payload     |
| `session_id`   | 4 bytes | Room/session identifier       |
| `reserved`     | 4 bytes | Reserved for future use       |

**Commands:** `FILE_META(1)` - `FILE_CHUNK(2)` - `CANCEL(3)` - `PING(4)` - `PONG(5)` - `RESUME(6)` - `AUTH(7)` - `AUTH_OK(8)` - `AUTH_FAIL(9)`

---

## Discovery Protocol

The engine uses **dual discovery** for maximum compatibility:

1. **UDP Broadcast** (`255.255.255.255:45454`) - works on regular WiFi networks
2. **UDP Multicast** (`239.255.45.45:45454`) - works across hotspot networks

Message format: `FLUXDROP|<session_id>|<port>|<instance_id>`

If neither discovery method works (e.g., restrictive networks or Android hotspot limitations), use **manual IP connect**. Both the Linux GUI and Android app support entering the sender's IP address and port directly.

---

## Integrating in Your Project

1. **Copy the source files:**
   - `include/` - all headers
   - `src/core_api.cpp`, `networking.cpp`, `transfer.cpp`, `security.cpp`, `packet.cpp`

2. **Link dependencies:** Boost.Asio, libsodium, nlohmann-json

3. **Example CMake:**
   ```cmake
   add_library(fluxdrop_core STATIC
       src/core_api.cpp src/networking.cpp src/transfer.cpp
       src/security.cpp src/packet.cpp)
   target_include_directories(fluxdrop_core PUBLIC include/)
   target_link_libraries(fluxdrop_core Boost::system sodium)
   ```
