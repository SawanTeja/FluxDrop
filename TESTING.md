# FluxDrop Testing Guide

All tests assume the project is already built. Every test requires **two terminal windows** on the same machine (loopback) unless noted otherwise.

---

## Test 1: Build Verification

```bash
# Linux CLI
cd ~/FluxDrop && mkdir -p build && cd build && cmake .. && make -j$(nproc)

# Linux GUI
cd ~/FluxDrop/linux && mkdir -p build && cd build && cmake .. && make -j$(nproc)

# Android
cd ~/FluxDrop/android && ./gradlew assembleDebug
```

**✅ Pass if:** All builds complete with no errors.

---

## Test 2: Single File Transfer (Loopback)

**Terminal 1 — Sender:**
```bash
echo "Hello FluxDrop" > /tmp/testfile.txt
cd /tmp && ~/FluxDrop/build/fluxdrop testfile.txt
```

**Terminal 2 — Receiver:**
```bash
mkdir -p /tmp/recv && cd /tmp/recv
~/FluxDrop/build/fluxdrop connect <ip> <port>
# Enter the PIN shown in Terminal 1, accept the file
```

**✅ Pass if:** `cat /tmp/recv/testfile.txt` outputs `Hello FluxDrop`

---

## Test 3: Auto-Discovery via `join`

**Terminal 1:** `cd /tmp && ~/FluxDrop/build/fluxdrop testfile.txt`

**Terminal 2:**
```bash
mkdir -p /tmp/recv_join && cd /tmp/recv_join
~/FluxDrop/build/fluxdrop join 482913
```

**✅ Pass if:** Receiver auto-discovers the sender and prompts for PIN.

---

## Test 4: Wrong PIN Authentication

Start a sender. Connect a receiver and enter a wrong PIN (e.g. `0000`).

**✅ Pass if:** Both sides print "Authentication FAILED" and exit. No files sent.

---

## Test 5: File Rejection

Connect and authenticate, then type `n` when prompted `Accept? (y/n)`.

**✅ Pass if:** Sender prints "Client rejected", no file appears on receiver.

---

## Test 6: Multi-File Transfer

```bash
echo "A" > /tmp/a.txt && echo "B" > /tmp/b.txt && echo "C" > /tmp/c.txt
cd /tmp && ~/FluxDrop/build/fluxdrop a.txt b.txt c.txt
```

**✅ Pass if:** Receiver gets all 3 files.

---

## Test 7: Directory Transfer

```bash
mkdir -p /tmp/testdir/subdir
echo "Root" > /tmp/testdir/root.txt
echo "Nested" > /tmp/testdir/subdir/nested.txt
cd /tmp && ~/FluxDrop/build/fluxdrop testdir/
```

**✅ Pass if:** Directory structure is preserved on receiver side.

---

## Test 8: Large File Transfer with Progress

```bash
dd if=/dev/urandom of=/tmp/large_test.bin bs=1M count=100
cd /tmp && ~/FluxDrop/build/fluxdrop large_test.bin
```

**✅ Pass if:** Live progress bar shows percentage and speed. Checksums match after transfer:
```bash
md5sum /tmp/large_test.bin /tmp/recv/large_test.bin
```

---

## Test 9: Transfer Resume (`.fluxpart`)

1. Start a 200MB transfer, interrupt with `Ctrl+C` mid-transfer
2. Verify `.fluxpart` file exists
3. Restart both sender and receiver
4. **✅ Pass if:** Resume is detected and transfer completes. Checksums match.

---

## Test 10: Disk Space Check

```bash
sudo mkdir -p /tmp/tinydisk
sudo mount -t tmpfs -o size=1m tmpfs /tmp/tinydisk
cd /tmp/tinydisk && ~/FluxDrop/build/fluxdrop connect <ip> <port>
```

**✅ Pass if:** Auto-rejects with "Insufficient disk space" message.

Cleanup: `sudo umount /tmp/tinydisk`

---

## Test 11: Invalid Path Handling

```bash
~/FluxDrop/build/fluxdrop /tmp/nonexistent_file.txt
```

**✅ Pass if:** Prints `Skipping invalid path` with 0 files queued.

---

## Test 12: Two-Machine LAN Transfer

Full integration test between two physical machines on the same network.

If auto-discovery doesn't work, open firewall ports:
```bash
# Fedora
sudo firewall-cmd --add-port=45454/udp && sudo firewall-cmd --add-port=<port>/tcp

# Ubuntu
sudo ufw allow 45454/udp && sudo ufw allow <port>/tcp
```

---

## Test 13: Hotspot Discovery (Android ↔ Linux)

Test with phone hotspot setups:

| # | Scenario | Steps |
|---|----------|-------|
| 1 | Laptop → Phone (broadcast) | Share from Linux GUI → phone should auto-discover |
| 2 | Phone → Laptop (multicast) | Share from Android → laptop should auto-discover via multicast |
| 3 | Manual IP: Phone → Laptop | Share from Android → on Linux click "Connect by IP" → enter IP:port |
| 4 | Manual IP: Laptop → Phone | Share from Linux → on Android tap "Connect by IP" → enter IP:port |
