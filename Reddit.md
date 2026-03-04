Title: Show r/cpp: I built FluxDrop - A cross-platform, secure P2P LAN file transfer tool in C++20 (Looking for feedback & testers!)

Body:

Hey everyone,

I've been working on FluxDrop, a fast and secure peer-to-peer file transfer tool for local networks, and I'm looking for some technical feedback and beta testers!

What is it?
It's a cross-platform tool allowing seamless file and directory transfers between Linux, Windows, and Android devices on the same LAN. No cloud servers are involved. The core engine is written purely in C++20 and exposes a thread-safe C API (fluxdrop_core.h) designed for easy FFI integration (currently used by the Android JNI bridge and Linux GUI).

Tech Stack & Core Features:
- Engine & Networking: C++20, Boost.Asio for async TCP transfers.
- Discovery: Dual-protocol auto-discovery using UDP Broadcast and Multicast, with a manual IP fallback tailored for restrictive mobile hotspots.
- Security: 4-digit PIN authentication using libsodium (BLAKE2b hashing).
- Protocol: Custom binary protocol supporting 64KB chunked file transfers, directory recursion, and automatic pause/resume for interrupted transfers (.fluxpart files).
- Clients: Linux/Windows CLI, Linux GTK4 GUI, and a native Android app (Jetpack Compose).

Why I built it:
I wanted a lightning-fast, entirely offline alternative to AirDrop that offers cross-platform support out of the box. By decoupling the asynchronous engine from the UI, I also aimed to create a modular architecture that others could integrate into their own projects.

What I need help with:
I would love for you to test out the software and tell me how it runs on your devices!

Did the auto-discovery work seamlessly? Were the transfer speeds fast? Did you run into any issues when trying to send files between different operating systems?

The repository includes a detailed README, API documentation, and a step-by-step TESTING.md guide with things you can test.

[Insert GitHub Repo Link Here]

I'd really appreciate it if you could give it a spin. Feel free to tear the codebase apart or let me know what features you think are missing. Happy to answer any implementation questions in the comments!
