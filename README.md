# ISO Flash 2.0

C++20 / Qt 6 application for writing hybrid Linux ISO images to USB drives on Linux.

## Current development status

**The production package is still fail-closed: physical writes are disabled by default.**

The privileged D-Bus service and GUI are connected. The service implements ISO descriptor transfer, caller identity checks, Polkit authorization, post-authorization target admission, owner-bound write jobs, cancellation, progress/completion signals, bounded writes, fsync, and SHA-256 read-back verification.

The destructive path is enabled only in a separate CI test executable. That executable is compiled with explicit test-only definitions which allow file-backed loop devices; the normal packaged service is compiled without ISO_FLASH_ENABLE_WRITES and returns WRITE_DISABLED from StartWrite. Do not use this development package to write physical media.

## Implemented components

- Qt 6 GUI selects ISO files and displays only USB candidates accepted by the privileged service.
- GUI passes the already-open ISO descriptor through D-Bus Unix FD; it never sends an arbitrary device path and never runs as root.
- System D-Bus service obtains caller UID/PID from the bus daemon and checks the live process UID and start-time identity before and after Polkit authorization.
- Polkit authorization uses the invocation's unique D-Bus sender as a system-bus-name subject and the fixed action org.iso_flash.write.
- Target admission resolves major/minor through sysfs, validates USB transport and safety metadata, opens the writable device with fixed restrictive flags, and binds the descriptor to its device identity and capacity.
- WriteJobRegistry binds each job to its D-Bus owner. Only the owner may cancel it; caller disappearance requests cancellation.
- D-Bus Progress and Finished signals are sent to the owning client.
- The write operation handles short writes and EINTR, tracks partial progress, calls fsync, and verifies the written byte range using SHA-256 read-back.
- Device errors are terminal; a failed or cancelled write can leave a partially written, unusable target.
- CI builds and installs both Debian packages, verifies purge/reinstallation and service restart, starts the real system Polkit service, and exercises the enabled write path only against disposable file-backed loop devices. A test-only EIO injection after the first 1 MiB verifies device-loss classification, partial-byte accounting, and the written ISO prefix.

## Build

Ubuntu 24.04 / 26.04 amd64:

    sudo apt update
    sudo apt install -y build-essential cmake ninja-build pkg-config libglib2.0-dev qt6-base-dev
    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
    cmake --build build --parallel 2
    ctest --test-dir build --output-on-failure

## Safety and release status

- Production physical writing remains disabled pending a manual desktop-session GUI smoke test, security review of the destructive path, and explicit release approval. The complete CI loop-device, Polkit, package, and injected device-loss suite passes.
- CI-only loop-device permission is isolated to iso-flash-service-loop-test; it is not compiled into the packaged service.
- No CI test writes to a physical drive.
- A cancelled or interrupted operation cannot roll back bytes already written.
- Only hybrid Linux ISO raw imaging is in scope. Windows installer ISO preparation requires a separate FAT32/NTFS-aware implementation.
- No GitHub Release has been published. Release preparation remains gated on a green final CI run and review of the destructive-path tests.

## Repository layout

- engine/ — device safety, D-Bus service, Polkit, write engine
- gui/ — Qt desktop application
- tests/ — unit, contract, D-Bus, Polkit, and loop-device integration tests
- packaging/ — Debian package metadata, D-Bus, systemd, and Polkit files
- docs/ — security and D-Bus interface documentation
