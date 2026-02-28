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

| Dependency    | Purpose               | Install (Ubuntu/Debian)              |
| ------------- | --------------------- | ------------------------------------ |
| CMake ≥ 3.10  | Build system           | `sudo apt install cmake`             |
| C++20 compiler| Core language          | `sudo apt install g++` (GCC 10+)     |
| Boost         | Networking (Asio)      | `sudo apt install libboost-all-dev`  |
| libsodium     | PIN hashing (BLAKE2b)  | `sudo apt install libsodium-dev`     |
| nlohmann-json | JSON file metadata     | `sudo apt install nlohmann-json3-dev`|
| pkg-config    | Dependency resolution  | `sudo apt install pkg-config`        |

### Install all at once

```bash
sudo apt update
sudo apt install cmake g++ libboost-all-dev libsodium-dev nlohmann-json3-dev pkg-config
```

---

## Building

```bash
# From the project root
mkdir -p build && cd build
cmake ..
make -j$(nproc)
```

The binary is produced at `build/fluxdrop`.

---

## Usage

FluxDrop has three modes based on command-line arguments:

### 1. Send Files (Server Mode)

```bash
# Send a single file
./fluxdrop myfile.txt

# Send multiple files
./fluxdrop file1.txt file2.pdf image.png

# Send an entire directory (recursive)
./fluxdrop my_folder/
```

**What happens:**
1. The server binds to a random TCP port and starts UDP broadcasting on port `45454`
2. A 4-digit PIN is displayed in the terminal
3. Waits for a client to connect and authenticate with the PIN
4. Sends each file sequentially, waiting for accept/reject per file

### 2. Receive Files — Auto-Discovery (Client `join` Mode)

```bash
./fluxdrop join <room_id>
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
./fluxdrop connect <ip> <port>
```

**What happens:**
1. Directly connects to the sender's IP and port (shown on sender's terminal)
2. Same PIN prompt and file accept/reject flow as `join` mode

---

## How to Test Everything

### Test 1: Build Verification

Confirms the project compiles without errors.

```bash
cd /home/sawan/FluxDrop
rm -rf build && mkdir build && cd build
cmake ..
make -j$(nproc)
```

**Expected:** Build completes with no errors, `fluxdrop` binary exists in `build/`.

```bash
ls -la build/fluxdrop
```

---

### Test 2: Single File Transfer (Loopback)

Tests core file send/receive on the same machine.

**Terminal 1 (Sender):**

```bash
# Create a test file
echo "Hello FluxDrop" > /tmp/testfile.txt

# Start sender
cd /tmp
./path/to/fluxdrop testfile.txt
```

**Expected output:**
```
Total files to transfer: 1
Listening on 192.168.x.x:XXXXX
┌──────────────────────┐
│  Room PIN: XXXX       │
└──────────────────────┘
```

**Terminal 2 (Receiver):**

```bash
mkdir -p /tmp/fluxdrop_recv && cd /tmp/fluxdrop_recv

# Use the IP and port from Terminal 1
./path/to/fluxdrop connect <ip> <port>
```

**Expected flow:**
```
Connected to peer!
Enter room PIN: <type the PIN from Terminal 1>
Authenticated! Waiting for file streams...

Incoming file: testfile.txt (15.0B)
Accept? (y/n) y
File accepted. Downloading...
100% | X.X MB/s | ETA 00:00
File transfer completed successfully.
Download complete!
```

**Verify:**

```bash
cat /tmp/fluxdrop_recv/testfile.txt
# Should print: Hello FluxDrop
```

---

### Test 3: Auto-Discovery via `join`

Tests UDP broadcast discovery.

**Terminal 1 (Sender):**

```bash
cd /tmp
./path/to/fluxdrop testfile.txt
```

Note the Room ID is `482913` (default).

**Terminal 2 (Receiver):**

```bash
mkdir -p /tmp/fluxdrop_join && cd /tmp/fluxdrop_join
./path/to/fluxdrop join 482913
```

**Expected:**
```
Scanning for room 482913 broadcasts...
Found host: 192.168.x.x room 482913
Connected to peer!
Enter room PIN: <type PIN>
Authenticated! Waiting for file streams...
```

Then the same file accept/reject flow as Test 2.

---

### Test 4: Wrong PIN Authentication

Tests that invalid PINs are rejected.

**Terminal 1 (Sender):**

```bash
cd /tmp
./path/to/fluxdrop testfile.txt
```

**Terminal 2 (Receiver):**

```bash
./path/to/fluxdrop connect <ip> <port>
# When prompted for PIN, enter a wrong PIN like: 0000
```

**Expected:**
- Receiver sees: `Authentication failed. Wrong PIN.`
- Sender sees: `Authentication FAILED. Wrong PIN.`
- Connection is terminated, no files are sent.

---

### Test 5: File Rejection

Tests that the receiver can reject incoming files.

**Terminal 1 (Sender):**

```bash
cd /tmp
./path/to/fluxdrop testfile.txt
```

**Terminal 2 (Receiver):**

```bash
./path/to/fluxdrop connect <ip> <port>
# Authenticate with correct PIN
# When prompted "Accept? (y/n)", type: n
```

**Expected:**
- Sender sees: `Client rejected testfile.txt.`
- Sender outputs: `All transfers completed.`
- Receiver sees: `File rejected.`
- No file is downloaded.

---

### Test 6: Multi-File Transfer

Tests sending multiple files in one session.

**Terminal 1 (Sender):**

```bash
echo "File A" > /tmp/a.txt
echo "File B" > /tmp/b.txt
echo "File C" > /tmp/c.txt

cd /tmp
./path/to/fluxdrop a.txt b.txt c.txt
```

**Expected output:**
```
Total files to transfer: 3
```

**Terminal 2 (Receiver):**

```bash
mkdir -p /tmp/fluxdrop_multi && cd /tmp/fluxdrop_multi
./path/to/fluxdrop connect <ip> <port>
# Authenticate, then accept/reject each file individually
```

**Expected:** Each file is offered sequentially. Accept all three and verify:

```bash
ls /tmp/fluxdrop_multi/
# a.txt  b.txt  c.txt
```

---

### Test 7: Directory Transfer

Tests recursive directory sending with path preservation.

**Terminal 1 (Sender):**

```bash
mkdir -p /tmp/testdir/subdir
echo "Root file" > /tmp/testdir/root.txt
echo "Sub file" > /tmp/testdir/subdir/nested.txt

cd /tmp
./path/to/fluxdrop testdir/
```

**Expected output:**
```
Queued directory: testdir/ (2 files)
Total files to transfer: 2
```

**Terminal 2 (Receiver):**

```bash
mkdir -p /tmp/fluxdrop_dir && cd /tmp/fluxdrop_dir
./path/to/fluxdrop connect <ip> <port>
# Authenticate and accept both files
```

**Verify structure is preserved:**

```bash
find /tmp/fluxdrop_dir/ -type f
# /tmp/fluxdrop_dir/testdir/root.txt
# /tmp/fluxdrop_dir/testdir/subdir/nested.txt
```

---

### Test 8: Large File Transfer with Progress

Tests transfer speed reporting and progress bar with a large file.

```bash
# Generate a 100MB test file
dd if=/dev/urandom of=/tmp/large_test.bin bs=1M count=100
```

**Terminal 1 (Sender):**

```bash
cd /tmp
./path/to/fluxdrop large_test.bin
```

**Terminal 2 (Receiver):**

```bash
mkdir -p /tmp/fluxdrop_large && cd /tmp/fluxdrop_large
./path/to/fluxdrop connect <ip> <port>
# Authenticate and accept
```

**Expected during transfer:**
```
42% | 823.5 MB/s | ETA 00:01
```

The progress line updates in-place (carriage return `\r`). On completion:
```
File transfer completed successfully.
Download complete!
```

**Verify integrity:**

```bash
md5sum /tmp/large_test.bin /tmp/fluxdrop_large/large_test.bin
# Both hashes should match
```

---

### Test 9: Transfer Resume (`.fluxpart`)

Tests resume of interrupted downloads.

**Step 1 — Start a large transfer and interrupt it:**

```bash
dd if=/dev/urandom of=/tmp/resume_test.bin bs=1M count=200
```

**Terminal 1 (Sender):**

```bash
cd /tmp
./path/to/fluxdrop resume_test.bin
```

**Terminal 2 (Receiver):**

```bash
mkdir -p /tmp/fluxdrop_resume && cd /tmp/fluxdrop_resume
./path/to/fluxdrop connect <ip> <port>
# Authenticate and accept the file
# Press Ctrl+C midway through the transfer
```

**Verify partial file:**

```bash
ls /tmp/fluxdrop_resume/
# resume_test.bin.fluxpart   <-- partial download exists
```

**Step 2 — Resume the transfer:**

Restart the sender with the same file, then reconnect:

**Terminal 1:** `./path/to/fluxdrop resume_test.bin`

**Terminal 2:**

```bash
cd /tmp/fluxdrop_resume
./path/to/fluxdrop connect <ip> <port>
# Authenticate. It should detect the .fluxpart file:
```

**Expected:**
```
Found partial download. XX.XMB out of 200.0MB downloaded.
Accept? (y/n) y
Resuming file transfer from offset...
```

**Verify:**

```bash
md5sum /tmp/resume_test.bin /tmp/fluxdrop_resume/resume_test.bin
# Hashes should match
```

---

### Test 10: Disk Space Check

Tests that files are auto-rejected when there isn't enough space.

To test this without filling your disk, you can use a tmpfs (RAM-based filesystem):

```bash
# Create a 1MB tmpfs mount
sudo mkdir -p /tmp/tinydisk
sudo mount -t tmpfs -o size=1m tmpfs /tmp/tinydisk
cd /tmp/tinydisk

# Now try receiving a 10MB file - should be auto-rejected
./path/to/fluxdrop connect <ip> <port>
```

**Expected:**
```
Incoming file: large_test.bin (10.0MB)
Error: Insufficient disk space! Requires 10.0MB but only 1.0MB available.
Rejecting file automatically.
```

**Cleanup:**

```bash
sudo umount /tmp/tinydisk
```

---

### Test 11: Invalid Path Handling

Tests that the sender handles bad file paths gracefully.

```bash
./path/to/fluxdrop /tmp/nonexistent_file.txt
```

**Expected:**
```
Skipping invalid path: /tmp/nonexistent_file.txt
Total files to transfer: 0
```

If all paths are invalid, the sender creates a dummy job for backwards compatibility.

---

### Test 12: Two-Machine LAN Transfer

The ultimate integration test — transfer between two separate machines.

**Prerequisites:**
- Both machines are on the same local network (WiFi or Ethernet)
- FluxDrop is built on both machines
- UDP broadcast port `45454` is not blocked by firewall

**Machine A (Sender):**

```bash
./fluxdrop important_document.pdf
# Note the IP, port, and PIN
```

**Machine B (Receiver):**

```bash
# Option 1: Auto-discovery
./fluxdrop join 482913

# Option 2: Direct connect
./fluxdrop connect <Machine-A-IP> <port>
```

**Firewall troubleshooting (if auto-discovery fails):**

```bash
# Allow UDP broadcast on port 45454
sudo ufw allow 45454/udp

# Allow the TCP port shown by the sender
sudo ufw allow <port>/tcp
```

---

## Quick Reference

| Action                    | Command                                  |
| ------------------------- | ---------------------------------------- |
| Send file(s)              | `./fluxdrop file1.txt file2.txt`         |
| Send directory            | `./fluxdrop my_folder/`                  |
| Receive (auto-discovery)  | `./fluxdrop join 482913`                 |
| Receive (direct connect)  | `./fluxdrop connect 192.168.1.5 34567`   |

## Troubleshooting

| Problem                         | Solution                                                     |
| ------------------------------- | ------------------------------------------------------------ |
| `join` hangs forever            | Ensure both machines are on the same subnet; check firewall allows UDP 45454 |
| Connection refused              | Verify sender is running; check TCP port isn't blocked       |
| `libsodium initialization failed` | Reinstall: `sudo apt install libsodium-dev`, rebuild        |
| Build fails on nlohmann/json    | Install: `sudo apt install nlohmann-json3-dev`               |
| Wrong PIN repeatedly            | Restart the sender to generate a new PIN                     |
| Files land in wrong directory   | `cd` to the desired download directory before running client |
| `.fluxpart` file left behind    | Partial download from interrupted transfer; resume or delete |
| `Skipping invalid path`        | Check that the file/directory path exists and is accessible  |
| Transfer speed is slow          | Both machines should ideally be on wired Ethernet; WiFi adds overhead |

---

