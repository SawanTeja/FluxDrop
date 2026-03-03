# FluxDrop

A fast, secure, cross-platform peer-to-peer file transfer tool for local networks. Transfer files between Linux, Windows, and Android devices on the same LAN with PIN-based authentication, chunked transfers with resume support, and real-time progress tracking.

## Features

- **Cross-Platform** - Linux (CLI and GTK4 GUI), Windows (CLI), Android (Jetpack Compose)
- **P2P LAN Transfer** - Direct TCP connections, no cloud required
- **Auto-Discovery** - UDP broadcast and multicast discovery for peers
- **Manual IP Connect** - Fallback for hotspot networks where discovery fails
- **PIN Authentication** - 4-digit PIN with BLAKE2b hashing via libsodium
- **Chunked Transfer** - 64KB chunks with a custom binary protocol
- **Resume Support** - Interrupted downloads save as `.fluxpart` files
- **Directory Transfer** - Recursively send directories preserving structure
- **Progress Tracking** - Live speed (MB/s), percentage, filename display
- **Disk Space Check** - Receiver validates space before accepting

---

## Project Structure

```
FluxDrop/
├── include/                  # Shared C++ headers (all platforms)
│   ├── fluxdrop_core.h       # C API for the transfer engine
│   ├── networking.hpp        # Server, Client, DiscoveryListener classes
│   ├── transfer.hpp          # File send/receive with progress
│   ├── security.hpp          # PIN generation & BLAKE2b hashing
│   └── protocol/             # Packet header & file metadata formats
│       ├── packet.hpp
│       └── file_meta.hpp
│
├── src/                      # Shared C++ source (all platforms)
│   ├── core_api.cpp          # C API implementation (fd_* functions)
│   ├── networking.cpp        # TCP server/client + UDP discovery
│   ├── transfer.cpp          # Chunked file I/O + progress callbacks
│   ├── security.cpp          # libsodium PIN hashing
│   ├── packet.cpp            # Binary protocol serialization
│   └── main.cpp              # CLI entry point (Linux/Windows)
│
├── linux/                    # Linux GTK4 GUI
│   ├── CMakeLists.txt
│   ├── include/ui/           # GUI panel headers
│   └── src/ui/               # GTK4 implementation
│       ├── main_window.cpp   # App shell, CSS, stack switcher
│       ├── file_sender.cpp   # Send panel (file picker → server)
│       ├── device_list.cpp   # Receive panel (discovery + connect)
│       └── transfer_dialog.cpp
│
├── android/                  # Android app (Jetpack Compose + JNI)
│   └── app/src/main/
│       ├── cpp/
│       │   ├── jni_bridge.cpp        # JNI ↔ C API bridge
│       │   └── third_party/          # Boost, libsodium, spdlog headers
│       └── java/dev/fluxdrop/app/
│           ├── bridge/FluxDropCore.kt  # Kotlin ↔ JNI interface
│           ├── ui/screens/
│           │   ├── SendScreen.kt       # Share files screen
│           │   └── ReceiveScreen.kt    # Receive files screen
│           └── ui/components/
│               └── TransferProgress.kt # Gradient progress bar
│
├── CMakeLists.txt            # Root CMake (CLI build)
├── TESTING.md                # Full test suite documentation
└── README.md                 # Project documentation
```

## Core Engine API

The transfer engine is exposed as a **C API** (`fluxdrop_core.h`), designed for easy FFI binding to any language (Kotlin/JNI, Python/ctypes, Rust/FFI, etc.).

See **[API.md](API.md)** for the full reference: types, functions, callbacks, protocol format, discovery protocol, and integration guide.

---

## Building

### Prerequisites

| Dependency    | Purpose              | Fedora                          | Ubuntu/Debian                  |
|---------------|----------------------|---------------------------------|--------------------------------|
| CMake ≥ 3.10  | Build system         | `sudo dnf install cmake`        | `sudo apt install cmake`       |
| C++20         | Core language        | `sudo dnf install gcc-c++`      | `sudo apt install g++`         |
| Boost         | Networking (Asio)    | `sudo dnf install boost-devel`  | `sudo apt install libboost-all-dev` |
| libsodium     | PIN hashing          | `sudo dnf install libsodium-devel` | `sudo apt install libsodium-dev` |
| nlohmann-json | JSON metadata        | `sudo dnf install json-devel`   | `sudo apt install nlohmann-json3-dev` |
| pkg-config    | Dep resolution       | `sudo dnf install pkgconf-pkg-config` | `sudo apt install pkg-config` |
| GTK4          | Linux GUI only       | `sudo dnf install gtk4-devel`   | `sudo apt install libgtk-4-dev` |

#### One-liner install

**Fedora:**
```bash
sudo dnf install cmake gcc-c++ boost-devel libsodium-devel json-devel pkgconf-pkg-config gtk4-devel
```

**Ubuntu/Debian:**
```bash
sudo apt install cmake g++ libboost-all-dev libsodium-dev nlohmann-json3-dev pkg-config libgtk-4-dev
```

**Windows (MSYS2 UCRT64):**
```bash
pacman -S mingw-w64-ucrt-x86_64-{gcc,cmake,pkgconf,boost,libsodium,nlohmann-json,gtk4}
```

### Build Commands

#### Linux CLI
```bash
cd ~/FluxDrop
mkdir -p build && cd build
cmake ..
make -j$(nproc)
# Binary: build/fluxdrop
```

#### Linux GUI
```bash
cd ~/FluxDrop/linux
mkdir -p build && cd build
cmake ..
make -j$(nproc)
# Binary: build/fluxdrop_gui
```

#### Windows (MSYS2)
```bash
cd /c/path/to/FluxDrop
mkdir -p build && cd build
cmake -G "MinGW Makefiles" ..
cmake --build . -j%NUMBER_OF_PROCESSORS%
# Binary: build/fluxdrop.exe
```

#### Android
```bash
cd ~/FluxDrop/android
./gradlew assembleDebug
# APK: app/build/outputs/apk/debug/app-debug.apk
```

---

## Usage

### CLI

```bash
# Send files
~/FluxDrop/build/fluxdrop file1.txt file2.pdf my_folder/

# Receive (auto-discovery)
~/FluxDrop/build/fluxdrop join 482913

# Receive (direct connect)
~/FluxDrop/build/fluxdrop connect <ip> <port>
```

### Linux GUI

Launch `fluxdrop_gui`. Use the **Send** tab to pick files and share (shows PIN). Use the **Receive** tab to discover senders or click **Connect by IP** for manual entry.

### Android

Open the app. Use the **Send** tab to select files. Use the **Receive** tab to discover senders. Tap a device or use **Connect by IP** if discovery does not work (e.g., due to Android UDP broadcast limitations on hotspots).

---

## Known Limitations and Issues

*   **Android UDP Broadcast:** Android devices often restrict or entirely block UDP broadcast packets, particularly when acting as a mobile hotspot. When sending files from an Android device to a Windows or Linux machine over an Android hotspot, automatic discovery will likely fail. 
    *   **Workaround:** Use the "Connect by IP" feature and manually enter the Android device's IP address and port to establish the connection.

---

## Troubleshooting

| Problem | Solution |
|---------|----------|
| `join` hangs forever | Ensure same subnet; check firewall allows UDP 45454 |
| Connection refused | Verify sender is running; check TCP port isn't blocked |
| Discovery fails on hotspot | Use "Connect by IP" and enter the sender's IP:port manually, common issue with Android devices |
| `libsodium initialization failed` | Reinstall libsodium-devel, rebuild |
| Build fails on nlohmann/json | Install `json-devel` (Fedora) or `nlohmann-json3-dev` (Ubuntu) |
| Wrong PIN repeatedly | Restart sender to generate a new PIN |
| `.fluxpart` file left behind | Partial download from interrupted transfer; resume or delete |
| Transfer speed is slow | Use wired Ethernet; WiFi adds overhead |

---

## Testing

See **[TESTING.md](TESTING.md)** for the complete test suite with step-by-step instructions.
