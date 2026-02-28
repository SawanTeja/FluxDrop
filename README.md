# FluxDrop

A fast, secure peer-to-peer file transfer tool for local networks. Transfer files and directories between machines on the same LAN with PIN-based authentication, chunked transfers with resume support, and real-time progress tracking.

## Features

- **P2P LAN Transfer** — Direct TCP connections between machines, no cloud required
- **Auto-Discovery** — UDP broadcast discovery to find peers on the network
- **PIN Authentication** — 4-digit PIN with BLAKE2b hashing via libsodium
- **Chunked Transfer** — 64KB chunks with a custom binary protocol (16-byte packet headers)
- **Resume Support** — Interrupted downloads save as `.fluxpart` files and can resume
- **Directory Transfer** — Recursively send entire directories preserving structure
- **Progress Tracking** — Live speed (MB/s), percentage, and ETA display
- **Disk Space Check** — Receiver validates available space before accepting files

---

## Architecture

```
┌─────────────────────────────────────────────────────┐
│                     main.cpp                        │
│  CLI entry point — parses args, queues TransferJobs │
└────────────┬───────────────────────────┬────────────┘
             │                           │
     ┌───────▼───────┐          ┌────────▼────────┐
     │  Server Mode  │          │  Client Mode    │
     │  (sender)     │          │  (receiver)     │
     └───────┬───────┘          └────────┬────────┘
             │                           │
     ┌───────▼───────────────────────────▼────────┐
     │            networking.cpp                   │
     │  Server::start()  Client::connect/join()    │
     │  UDP broadcast, PIN auth, transfer loop     │
     └───────┬───────────────────────────┬─────────┘
             │                           │
     ┌───────▼────────┐         ┌────────▼────────┐
     │  transfer.cpp  │         │  security.cpp   │
     │  Send/receive  │         │  PIN gen/hash   │
     │  files+headers │         │  via libsodium  │
     └───────┬────────┘         └─────────────────┘
             │
     ┌───────▼────────┐
     │   packet.cpp   │
     │  Serialize /   │
     │  deserialize   │
     │  16-byte hdrs  │
     └────────────────┘
```

### Protocol Packet Format

| Field          | Size    | Description                   |
| -------------- | ------- | ----------------------------- |
| `command`      | 4 bytes | Command type (network order)  |
| `payload_size` | 4 bytes | Size of following payload     |
| `session_id`   | 4 bytes | Room/session identifier       |
| `reserved`     | 4 bytes | Reserved for future use       |

**Command Types:** `FILE_META(1)`, `FILE_CHUNK(2)`, `CANCEL(3)`, `PING(4)`, `PONG(5)`, `RESUME(6)`, `AUTH(7)`, `AUTH_OK(8)`, `AUTH_FAIL(9)`

---

## Prerequisites

| Dependency    | Purpose               | Install (Fedora)                         | Install (Ubuntu/Debian)              |
| ------------- | --------------------- | ---------------------------------------- | ------------------------------------ |
| CMake ≥ 3.10  | Build system           | `sudo dnf install cmake`                 | `sudo apt install cmake`             |
| C++20 compiler| Core language          | `sudo dnf install gcc-c++`               | `sudo apt install g++`               |
| Boost         | Networking (Asio)      | `sudo dnf install boost-devel`           | `sudo apt install libboost-all-dev`  |
| libsodium     | PIN hashing (BLAKE2b)  | `sudo dnf install libsodium-devel`       | `sudo apt install libsodium-dev`     |
| nlohmann-json | JSON file metadata     | `sudo dnf install json-devel`            | `sudo apt install nlohmann-json3-dev`|
| pkg-config    | Dependency resolution  | `sudo dnf install pkgconf-pkg-config`    | `sudo apt install pkg-config`        |

### Install all at once

**Fedora:**
```bash
sudo dnf install cmake gcc-c++ boost-devel libsodium-devel json-devel pkgconf-pkg-config
```

**Ubuntu/Debian:**
```bash
sudo apt update
sudo apt install cmake g++ libboost-all-dev libsodium-dev nlohmann-json3-dev pkg-config
```

---

## Building

```bash
cd ~/FluxDrop
mkdir -p build && cd build
cmake ..
make -j$(nproc)
```

The binary is produced at `~/FluxDrop/build/fluxdrop`.

---

## Usage

FluxDrop has three modes based on command-line arguments:

### 1. Send Files (Server Mode)

```bash
cd /tmp
~/FluxDrop/build/fluxdrop myfile.txt
```

You can send multiple files or entire directories:

```bash
~/FluxDrop/build/fluxdrop file1.txt file2.pdf image.png
~/FluxDrop/build/fluxdrop my_folder/
```

**What happens:**
1. The server binds to a random TCP port and starts UDP broadcasting on port `45454`
2. A 4-digit PIN is displayed in the terminal
3. Waits for a client to connect and authenticate with the PIN
4. Sends each file sequentially, waiting for accept/reject per file

### 2. Receive Files — Auto-Discovery (Client `join` Mode)

```bash
~/FluxDrop/build/fluxdrop join 482913
```

**What happens:**
1. Listens on UDP port `45454` for broadcast messages
2. When a matching `room_id` is found, auto-connects to the sender
3. Prompts for the PIN displayed on the sender's terminal
4. For each incoming file, prompts `Accept? (y/n)`
5. Downloads accepted files to the current working directory

> **Note:** The default `room_id` is `482913`. The sender always uses this unless changed in code.

### 3. Receive Files — Direct Connect (Client `connect` Mode)

```bash
~/FluxDrop/build/fluxdrop connect <ip> <port>
```

**What happens:**
1. Directly connects to the sender's IP and port (shown on sender's terminal)
2. Same PIN prompt and file accept/reject flow as `join` mode

---

## How to Test Everything

> **Important:** All tests below assume you have already built the project. The binary is at:
> ```
> ~/FluxDrop/build/fluxdrop
> ```
> Every test requires **two terminal windows** open on the same machine (loopback testing).
> Replace `<ip>` and `<port>` with the values shown by the sender's output.

---

### Test 1: Build Verification

Confirms the project compiles without errors.

```bash
cd ~/FluxDrop
rm -rf build && mkdir build && cd build
cmake ..
make -j$(nproc)
```

**✅ Pass if:** Build completes with no errors and the binary exists:

```bash
ls -la ~/FluxDrop/build/fluxdrop
```

---

### Test 2: Single File Transfer (Loopback)

Tests core file send/receive on the same machine.

**Terminal 1 — Sender:**

```bash
echo "Hello FluxDrop" > /tmp/testfile.txt
cd /tmp
~/FluxDrop/build/fluxdrop testfile.txt
```

You'll see output like:
```
Total files to transfer: 1
Listening on 192.168.1.42:53721
┌──────────────────────┐
│  Room PIN: 4827       │
└──────────────────────┘
```

**Terminal 2 — Receiver (use the IP and port from above):**

```bash
mkdir -p /tmp/recv && cd /tmp/recv
~/FluxDrop/build/fluxdrop connect 192.168.1.42 53721
```

Then follow the prompts:
```
Connected to peer!
Enter room PIN: 4827        ← type the PIN shown in Terminal 1
Authenticated! Waiting for file streams...

Incoming file: testfile.txt (15.0B)
Accept? (y/n) y             ← type y
File accepted. Downloading...
100% | X.X MB/s | ETA 00:00
File transfer completed successfully.
Download complete!
```

**✅ Pass if:**

```bash
cat /tmp/recv/testfile.txt
# Output: Hello FluxDrop
```

---

### Test 3: Auto-Discovery via `join`

Tests UDP broadcast discovery.

**Terminal 1 — Sender:**

```bash
cd /tmp
~/FluxDrop/build/fluxdrop testfile.txt
```

Note the PIN. The default Room ID is `482913`.

**Terminal 2 — Receiver:**

```bash
mkdir -p /tmp/recv_join && cd /tmp/recv_join
~/FluxDrop/build/fluxdrop join 482913
```

**✅ Pass if:** Receiver auto-discovers the sender:
```
Scanning for room 482913 broadcasts...
Found host: 192.168.x.x room 482913
Connected to peer!
Enter room PIN:
```

Then same accept flow as Test 2.

---

### Test 4: Wrong PIN Authentication

Tests that invalid PINs are rejected.

**Terminal 1 — Sender:**

```bash
cd /tmp
~/FluxDrop/build/fluxdrop testfile.txt
```

**Terminal 2 — Receiver:**

```bash
~/FluxDrop/build/fluxdrop connect <ip> <port>
# When prompted, enter a WRONG PIN like: 0000
```

**✅ Pass if:**
- Receiver prints: `Authentication failed. Wrong PIN.`
- Sender prints: `Authentication FAILED. Wrong PIN.`
- Both sides exit. No files sent.

---

### Test 5: File Rejection

Tests that the receiver can decline incoming files.

**Terminal 1 — Sender:**

```bash
cd /tmp
~/FluxDrop/build/fluxdrop testfile.txt
```

**Terminal 2 — Receiver:**

```bash
mkdir -p /tmp/recv_reject && cd /tmp/recv_reject
~/FluxDrop/build/fluxdrop connect <ip> <port>
# Authenticate with the correct PIN
# When prompted "Accept? (y/n)", type: n
```

**✅ Pass if:**
- Sender prints: `Client rejected testfile.txt.` → `All transfers completed.`
- Receiver prints: `File rejected.`
- No file appears in `/tmp/recv_reject/`

---

### Test 6: Multi-File Transfer

Tests sending multiple files in one session.

**Terminal 1 — Sender:**

```bash
echo "File A" > /tmp/a.txt
echo "File B" > /tmp/b.txt
echo "File C" > /tmp/c.txt
cd /tmp
~/FluxDrop/build/fluxdrop a.txt b.txt c.txt
```

Sender should print `Total files to transfer: 3`.

**Terminal 2 — Receiver:**

```bash
mkdir -p /tmp/recv_multi && cd /tmp/recv_multi
~/FluxDrop/build/fluxdrop connect <ip> <port>
# Authenticate, then accept each of the 3 files
```

**✅ Pass if:**

```bash
ls /tmp/recv_multi/
# a.txt  b.txt  c.txt
```

---

### Test 7: Directory Transfer

Tests recursive directory sending with structure preserved.

**Terminal 1 — Sender:**

```bash
mkdir -p /tmp/testdir/subdir
echo "Root file" > /tmp/testdir/root.txt
echo "Sub file" > /tmp/testdir/subdir/nested.txt
cd /tmp
~/FluxDrop/build/fluxdrop testdir/
```

Sender shows: `Queued directory: testdir/ (2 files)`

**Terminal 2 — Receiver:**

```bash
mkdir -p /tmp/recv_dir && cd /tmp/recv_dir
~/FluxDrop/build/fluxdrop connect <ip> <port>
# Authenticate and accept both files
```

**✅ Pass if:** Directory structure is preserved:

```bash
find /tmp/recv_dir/ -type f
# /tmp/recv_dir/testdir/root.txt
# /tmp/recv_dir/testdir/subdir/nested.txt
```

---

### Test 8: Large File Transfer with Progress

Tests progress bar (speed + ETA) with a big file.

**Setup:**

```bash
dd if=/dev/urandom of=/tmp/large_test.bin bs=1M count=100
```

**Terminal 1 — Sender:**

```bash
cd /tmp
~/FluxDrop/build/fluxdrop large_test.bin
```

**Terminal 2 — Receiver:**

```bash
mkdir -p /tmp/recv_large && cd /tmp/recv_large
~/FluxDrop/build/fluxdrop connect <ip> <port>
# Authenticate and accept
```

**✅ Pass if:** You see a live updating progress line like:
```
42% | 823.5 MB/s | ETA 00:01
```

And after completion, checksums match:

```bash
md5sum /tmp/large_test.bin /tmp/recv_large/large_test.bin
```

---

### Test 9: Transfer Resume (`.fluxpart`)

Tests that interrupted downloads can be resumed.

**Setup:**

```bash
dd if=/dev/urandom of=/tmp/resume_test.bin bs=1M count=200
```

**Step 1 — Start transfer and interrupt mid-way:**

**Terminal 1:** `cd /tmp && ~/FluxDrop/build/fluxdrop resume_test.bin`

**Terminal 2:**

```bash
mkdir -p /tmp/recv_resume && cd /tmp/recv_resume
~/FluxDrop/build/fluxdrop connect <ip> <port>
# Authenticate, accept the file, then press Ctrl+C midway
```

Verify partial file exists:

```bash
ls /tmp/recv_resume/
# resume_test.bin.fluxpart   ← partial download
```

**Step 2 — Resume:**

**Terminal 1:** `cd /tmp && ~/FluxDrop/build/fluxdrop resume_test.bin` (restart sender)

**Terminal 2:**

```bash
cd /tmp/recv_resume
~/FluxDrop/build/fluxdrop connect <ip> <port>
# Authenticate
```

**✅ Pass if:** It detects the partial file and resumes:
```
Found partial download. XX.XMB out of 200.0MB downloaded.
Accept? (y/n) y
Resuming file transfer from offset...
```

After completion, verify:

```bash
md5sum /tmp/resume_test.bin /tmp/recv_resume/resume_test.bin
# Both hashes should match
```

---

### Test 10: Disk Space Check

Tests auto-rejection when disk space is insufficient.

```bash
# Create a tiny 1MB filesystem
sudo mkdir -p /tmp/tinydisk
sudo mount -t tmpfs -o size=1m tmpfs /tmp/tinydisk
cd /tmp/tinydisk

# Try to receive a large file (sender must be running with a >1MB file)
~/FluxDrop/build/fluxdrop connect <ip> <port>
```

**✅ Pass if:**
```
Incoming file: large_test.bin (100.0MB)
Error: Insufficient disk space! Requires 100.0MB but only 1.0MB available.
Rejecting file automatically.
```

**Cleanup:**
```bash
sudo umount /tmp/tinydisk
```

---

### Test 11: Invalid Path Handling

Tests that the sender handles nonexistent files gracefully.

```bash
~/FluxDrop/build/fluxdrop /tmp/nonexistent_file.txt
```

**✅ Pass if:**
```
Skipping invalid path: /tmp/nonexistent_file.txt
Total files to transfer: 0
```

---

### Test 12: Two-Machine LAN Transfer

Full integration test between two physical machines on the same network.

**Machine A (Sender):**

```bash
~/FluxDrop/build/fluxdrop important_document.pdf
# Note the IP, port, and PIN
```

**Machine B (Receiver):**

```bash
# Option 1: Auto-discovery
~/FluxDrop/build/fluxdrop join 482913

# Option 2: Direct connect
~/FluxDrop/build/fluxdrop connect <Machine-A-IP> <port>
```

**If auto-discovery doesn't work**, open firewall ports:

```bash
# Fedora (firewalld)
sudo firewall-cmd --add-port=45454/udp
sudo firewall-cmd --add-port=<port>/tcp

# Ubuntu (ufw)
sudo ufw allow 45454/udp
sudo ufw allow <port>/tcp
```

---

## Quick Reference

| Action                    | Command                                                  |
| ------------------------- | -------------------------------------------------------- |
| Send file(s)              | `~/FluxDrop/build/fluxdrop file1.txt file2.txt`          |
| Send directory            | `~/FluxDrop/build/fluxdrop my_folder/`                   |
| Receive (auto-discovery)  | `~/FluxDrop/build/fluxdrop join 482913`                  |
| Receive (direct connect)  | `~/FluxDrop/build/fluxdrop connect 192.168.1.5 34567`    |

## Troubleshooting

| Problem                         | Solution                                                     |
| ------------------------------- | ------------------------------------------------------------ |
| `join` hangs forever            | Ensure both machines are on the same subnet; check firewall allows UDP 45454 |
| Connection refused              | Verify sender is running; check TCP port isn't blocked       |
| `libsodium initialization failed` | Reinstall libsodium-devel, rebuild                        |
| Build fails on nlohmann/json    | Install `json-devel` (Fedora) or `nlohmann-json3-dev` (Ubuntu) |
| Wrong PIN repeatedly            | Restart the sender to generate a new PIN                     |
| Files land in wrong directory   | `cd` to the desired download directory before running client |
| `.fluxpart` file left behind    | Partial download from interrupted transfer; resume or delete |
| `Skipping invalid path`        | Check that the file/directory path exists and is accessible  |
| Transfer speed is slow          | Both machines should ideally be on wired Ethernet; WiFi adds overhead |

