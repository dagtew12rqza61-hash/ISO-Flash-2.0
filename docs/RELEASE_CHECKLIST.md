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

- [ ] Run the latest final CI after the offscreen GUI smoke test and current documentation changes.
- [ ] Manually launch the GUI in a real desktop session and verify device discovery, ISO selection, Polkit denial/approval, progress, cancellation, and error display.
- [ ] Review the destructive write path, target safety checks, systemd capabilities, D-Bus policy, and Polkit action.
- [ ] Confirm the release artifact contains both packages and record the artifact SHA-256.
- [ ] Create tag v2.0.0 only after every required check is complete. The GitHub Actions workflow publishes the two .deb files when a v* tag is pushed.

## Package behavior and test boundary

The packaged D-Bus service enables USB writes only through the Polkit-protected StartWrite method. The GUI remains unprivileged. The standalone low-level CLI remains fail-closed.

CI uses only disposable file-backed loop devices. The loop-device exception and injected device-loss hook are compiled only into the CI test service; they are not present in the packaged service. No physical USB drive has been written by CI.

Only hybrid Linux ISO raw imaging is supported. Windows installer ISO preparation requires a separate FAT32/NTFS-aware workflow.
