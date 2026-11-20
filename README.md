# ISO Flash 2.0

C++20 / Qt 6 Linux USB image writer project.

## Development status

**Early development — physical writing is deliberately disabled.** The current GUI can select an ISO and list removable block-device candidates. The writer's `--check DEVICE` mode performs read-only inspection; the destructive `--write` mode exits without opening or modifying any device.

A narrow system D-Bus service now exposes read-only target inspection by block-device major/minor number. It is system-bus activated and runs under a hardened systemd unit. It does not expose a write method and does not accept arbitrary device paths. See [docs/DBUS_READ_ONLY_SERVICE.md](docs/DBUS_READ_ONLY_SERVICE.md).

This is not yet a write authorization boundary. Polkit write authorization, descriptor-bound writable-device checks, full bidirectional block topology validation, hot-unplug race handling, asynchronous job ownership, cancellation, and read-back verification remain incomplete. No physical-media write should be enabled until these controls and destructive-path tests pass.

## Build (Ubuntu 24.04 / 26.04 amd64)

```bash
sudo apt update
sudo apt install -y build-essential cmake ninja-build pkg-config libglib2.0-dev qt6-base-dev
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
```

## Current milestones

- [x] Qt 6 desktop shell
- [x] Initial Linux block-device guard prototype
- [x] Synthetic sysfs dependency traversal regression tests
- [x] Strict sysfs capacity metadata parsing
- [x] Injected block-device metadata/read failure tests (`EIO`, `ENODEV`)
- [x] Snapshot revalidation tests for device replacement, capacity changes, and read-only transitions
- [x] Fail-closed handling for filesystem metadata exceptions
- [x] Separate writer executable boundary
- [x] Disable physical writes in development builds
- [x] Initial read-only system D-Bus inspection service
- [x] System-bus activation and hardened systemd unit
- [x] Documented D-Bus + Polkit privileged-write security architecture
- [ ] Polkit-authorized write service
- [ ] Descriptor-bound writable device validation
- [ ] Complete bidirectional block-device topology validation
- [ ] Asynchronous writing with safe cancellation and progress
- [ ] SHA-256 and read-back verification
- [x] Loop-device rejection smoke test (no writes)
- [ ] Destructive-path tests using disposable loop devices
- [ ] Separate Windows installer media preparation
- [ ] Verified Debian packages and signed release

Raw imaging is intended for hybrid Linux ISO images. Windows installer ISO preparation requires a separate implementation and is not currently supported.

## Safety

Do not use this development build to modify physical media. Never select a drive containing data you need. A successful build is not proof of boot compatibility or production safety.

## License

MIT
