# ISO Flash 2.0

C++20 / Qt 6 Linux USB image writer project.

## Current implementation status

This repository starts the new implementation. It contains a Qt desktop shell, a separate privileged writer executable, basic sysfs block-device checks, streaming writes, and an initial CMake build.

**Not production-ready. Do not use with valuable data or a system disk.** The current baseline still needs independent security review and automated tests. SHA-256 reporting, byte-for-byte read-back verification, robust hot-unplug handling, cancellation, device identity revalidation after privilege escalation, mount/swap ancestry coverage, and Windows installer media preparation are not complete. GUI progress is currently a basic prototype.

## Build

```bash
sudo apt install build-essential cmake ninja-build qt6-base-dev
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

The writer is invoked through pkexec by the GUI and must be installed with root-owned package permissions. Do not run it manually against a real disk during development.

## Scope

Raw image writing is intended for hybrid Linux ISO images. Windows installer ISO preparation is a separate feature and is not implemented yet.

## License

MIT
