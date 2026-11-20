# ISO Flash 2.0

C++20 / Qt 6 Linux USB image writer project.

## Development status

**Early development — physical writing is deliberately disabled.** The current GUI can select an ISO and list removable block-device candidates. The privileged writer executable exits without opening or modifying any device.

This is intentional. The existing device guard is not yet a complete authorization boundary: device identity must be revalidated after privilege escalation and bound to the opened block-device file descriptor; read-only state, parent/child device topology, hot-unplug races, and cancellation semantics need additional coverage. No physical-media write should be enabled until these controls and destructive-path tests pass.

## Build (Ubuntu 24.04 / 26.04 amd64)

```bash
sudo apt update
sudo apt install -y build-essential cmake ninja-build qt6-base-dev
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
```

## Current milestones

- [x] Qt 6 desktop shell
- [x] Initial Linux block-device guard prototype
- [x] Separate writer executable boundary
- [x] Disable physical writes in development builds
- [ ] Hardened device identity and topology validation
- [ ] Narrow polkit-authorized helper
- [ ] Asynchronous writing with safe cancellation and progress
- [ ] SHA-256 and read-back verification
- [ ] Automated tests using disposable loop devices
- [ ] Separate Windows installer media preparation
- [ ] Verified Debian packages and signed release

Raw imaging is intended for hybrid Linux ISO images. Windows installer ISO preparation requires a separate implementation and is not currently supported.

## Safety

Do not use this development build to modify physical media. Never select a drive containing data you need. A successful build is not proof of boot compatibility or production safety.

## License

MIT
