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

- [x] Final package CI passed on commit `4f661459a23499e8e3fb536505e410cba3f57d7e` (run #377): build, regression suite, both Debian packages, install/purge/reinstall, D-Bus, Polkit, loop-device write lifecycle, and artifact upload.
- [ ] Manually launch the GUI in a real desktop session and verify device discovery, ISO selection, Polkit denial/approval, progress, cancellation, and error display.
- [ ] Review the destructive write path, target safety checks, systemd capabilities, D-Bus policy, and Polkit action.
- [ ] Confirm the release artifact contains both packages and record the artifact SHA-256.
- [ ] Create tag v2.0.0 only after every required check is complete. The GitHub Actions workflow publishes the two .deb files when a v* tag is pushed.

## Latest verified package artifact

- GitHub Actions run: https://github.com/dagtew12rqza61-hash/ISO-Flash-2.0/actions/runs/44746061638
- Artifact: `iso-flash-deb-amd64` (two amd64 Debian packages, version 2.0.0)
- Artifact SHA-256: `07b6a84be8e90d034312f2013c46667e0e37ab87ae4787956f369a96d8bddc30`
- `iso-flash-engine_2.0.0_amd64.deb` SHA-256: `5f9264740ec9c95af81593caf7c30eb01ab2e29b3aaa223d0072df5f9402875b`
- `iso-flash_2.0.0_amd64.deb` SHA-256: `4ee76cb9724444482a992c13897184231abe082ed469a4fd503f2f50a8ee6fd1`

## Package behavior and test boundary

The packaged D-Bus service enables USB writes only through the Polkit-protected StartWrite method. The GUI remains unprivileged. The standalone low-level CLI remains fail-closed.

CI uses only disposable file-backed loop devices. The loop-device exception and injected device-loss hook are compiled only into the CI test service; they are not present in the packaged service. No physical USB drive has been written by CI.

Only hybrid Linux ISO raw imaging is supported. Windows installer ISO preparation requires a separate FAT32/NTFS-aware workflow.
