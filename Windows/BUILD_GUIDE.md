# FluxDrop Windows Build Guide (Qt6)

Complete step-by-step tutorial for building the FluxDrop Qt6 Windows GUI from source.

---

## Prerequisites

### 1. Install MSYS2

Download and install MSYS2 from [https://www.msys2.org/](https://www.msys2.org/)

Default install path: `C:\msys64`

After installation, open **MSYS2 UCRT64** terminal and update the package database:

```bash
pacman -Syu
```

Close the terminal if prompted and reopen, then run again:

```bash
pacman -Su
```

### 2. Install Build Tools & Dependencies

In the **MSYS2 UCRT64** terminal, install everything in one command:

```bash
pacman -S --needed \
    mingw-w64-ucrt-x86_64-toolchain \
    mingw-w64-ucrt-x86_64-cmake \
    mingw-w64-ucrt-x86_64-ninja \
    mingw-w64-ucrt-x86_64-qt6-base \
    mingw-w64-ucrt-x86_64-boost \
    mingw-w64-ucrt-x86_64-libsodium \
    mingw-w64-ucrt-x86_64-nlohmann-json \
    mingw-w64-ucrt-x86_64-pkg-config
```

> **Note:** If you prefer the **MinGW64** environment instead of UCRT64, replace all `ucrt` with `x86_64` in the package names and use the MSYS2 MinGW64 terminal instead.

### 3. Verify Installation

```bash
gcc --version
cmake --version
qmake6 --version
pkg-config --modversion libsodium
```

All commands should produce version output without errors.

---

## Building

### Step 1: Build the Engine (Static Library)

The Engine is a platform-independent C++ core library that both Linux and Windows GUIs link against.

```bash
cd /c/Tejashvi/FluxDrop/Engine

rm -rf build && mkdir build

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

**Verify:** `build/libfluxdrop_core.a` should exist:

```bash
ls -la build/libfluxdrop_core.a
```

### Step 2: Build the Qt6 GUI

```bash
cd /c/Tejashvi/FluxDrop/Windows

rm -rf build && mkdir build

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

**Verify:** `build/fluxdrop_gui.exe` should exist:

```bash
ls -la build/fluxdrop_gui.exe
```

### Step 3: Test Run

```bash
cd build
./fluxdrop_gui.exe
```

The FluxDrop window should appear with the dark purple theme, Send File / Receive tabs, and the FluxDrop logo icon.

---

## Deployment (Creating a Standalone Package)

To run FluxDrop on machines without MSYS2, you need to bundle Qt6 runtime DLLs.

### Option A: Using the deploy script (Recommended)

A `deploy.sh` script is provided that handles everything automatically:

```bash
cd /c/Tejashvi/FluxDrop/Windows
bash deploy.sh
```

This creates a `dist/` folder with the standalone app.

### Option B: Using windeployqt6

```bash
mkdir -p dist
cp build/fluxdrop_gui.exe dist/
windeployqt6 dist/fluxdrop_gui.exe
cp -r assets dist/
```

### Option C: Manual DLL bundling

```bash
mkdir -p dist
cp build/fluxdrop_gui.exe dist/

# Copy all MinGW DLLs
ldd build/fluxdrop_gui.exe | grep "$MINGW_PREFIX" | awk '{print $3}' | while read dll; do
    cp "$dll" dist/
done

# Copy Qt6 platform plugin (required!)
QT_PLUGIN_PATH=$(qmake6 -query QT_INSTALL_PLUGINS)
mkdir -p dist/platforms
cp "$QT_PLUGIN_PATH/platforms/qwindows.dll" dist/platforms/

# Copy assets
cp -r assets dist/
```

### Verify

Navigate to the `dist/` folder in Windows Explorer and double-click `fluxdrop_gui.exe`.

---

## Creating a Single-File Executable

To create a single `FluxDrop.exe` self-extracting launcher:

### Using PowerShell

```powershell
cd C:\Tejashvi\FluxDrop\Windows
.\make_standalone.ps1
```

This will:
1. Build `dist/` with all DLLs
2. Compress into `app.zip`
3. Compile a C# launcher that embeds the zip
4. Output `FluxDrop.exe`

### Manual steps

See `make_standalone.ps1` for the full process.

---

## Troubleshooting

### "Qt platform plugin could not be initialized"

The `platforms/qwindows.dll` plugin is missing. Make sure:
- You used `deploy.sh` or `windeployqt6`, OR
- You manually copied `platforms/qwindows.dll` next to the exe

### "Cannot find -lfluxdrop_core"

The Engine library hasn't been built. Run Step 1 first.

### CMake can't find Qt6

Make sure you're running from the **MSYS2 UCRT64** terminal and that `mingw-w64-ucrt-x86_64-qt6-base` is installed.

### Missing DLLs at runtime

Run `ldd build/fluxdrop_gui.exe` to list all dependencies. Copy any missing DLLs from `C:\msys64\ucrt64\bin\`.

---

## Project Structure

```
FluxDrop/
├── Engine/          # Platform-independent core library
│   ├── CMakeLists.txt
│   ├── include/     # Core headers (fluxdrop_core.h, networking.hpp, logger.hpp, etc.)
│   └── src/         # Core implementation
├── Windows/         # Qt6 Windows GUI (you are here)
│   ├── BUILD_GUIDE.md
│   ├── CMakeLists.txt
│   ├── deploy.sh       # DLL bundling script
│   ├── make_standalone.ps1  # Single-exe packaging
│   ├── assets/      # Logo files
│   ├── include/     # GUI headers
│   └── src/         # GUI implementation
├── Linux/           # GTK4 Linux GUI
└── android/         # Android app
```
