# ISO Flash 2.0 — Release v2.0.0 checklist

**Release status: prepared, not published.** No tag or GitHub Release has been created.

## Automated checks already passing

- [x] C++ regression suite (27 tests).
- [x] Real system D-Bus and Polkit denial path.
- [x] Authorized D-Bus StartWrite using a read-only Unix FD.
- [x] Owner-only cancellation and client-disconnect cancellation.
- [x] Disposable file-backed loop-device write, fsync, and SHA-256 read-back.
- [x] Test-only device-loss injection after the first 1 MiB, including ENODEV classification and partial-byte accounting.
- [x] Loop devices rejected by the normal USB target guard.
- [x] Build both amd64 Debian packages.
- [x] Install, purge, verify package-file removal, and reinstall both packages.
- [x] systemd service start/restart and Polkit policy checks.
- [x] D-Bus manager interface and signal policy checks.

## Required before publishing

- [x] Final package CI passed on commit `ebbf6c17dc50ed77251f8f48a7a0b21e4af4a4e9` (run #379): build, regression suite, both Debian packages, install/purge/reinstall, D-Bus, Polkit, loop-device write lifecycle, and artifact upload.
- [ ] Manually launch the GUI in a real desktop session and verify device discovery, ISO selection, Polkit denial/approval, progress, cancellation, and error display.
- [ ] Review the destructive write path, target safety checks, systemd capabilities, D-Bus policy, and Polkit action.
- [ ] Confirm the release artifact contains both packages and record the artifact SHA-256.
- [ ] Create tag v2.0.0 only after every required check is complete. The GitHub Actions workflow publishes the two .deb files when a v* tag is pushed.

## Latest verified package artifact

- GitHub Actions run: https://github.com/dagtew12rqza61-hash/ISO-Flash-2.0/actions/runs/44746338726
- Artifact: `iso-flash-deb-amd64` (two amd64 Debian packages, version 2.0.0)
- Artifact SHA-256: `ebab9771bd665864468cb07ebe2643fac4879ef32fcf49a9e084374b2f7c8988`
- `iso-flash-engine_2.0.0_amd64.deb` SHA-256: `24ce3266f3dcd154705d270f9bd8ca02dbb6998a0f8d80160fc4cbe0765dc020`
- `iso-flash_2.0.0_amd64.deb` SHA-256: `bdfba0709360975690e4bc2070e052c072596add3e161ac41fb3e4340992496e`

## Package behavior and test boundary

The packaged D-Bus service enables USB writes only through the Polkit-protected StartWrite method. The GUI remains unprivileged. The standalone low-level CLI remains fail-closed.

CI uses only disposable file-backed loop devices. The loop-device exception and injected device-loss hook are compiled only into the CI test service; they are not present in the packaged service. No physical USB drive has been written by CI.

Only hybrid Linux ISO raw imaging is supported. Windows installer ISO preparation requires a separate FAT32/NTFS-aware workflow.
