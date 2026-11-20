# Privileged write architecture and verification status

## Security boundary

The Qt GUI remains unprivileged. It never runs as root and never invokes a shell command with a device path. The system D-Bus service owns the writable target descriptor for the complete operation. Each write request is authorized independently by Polkit.

The normal packaged service is deliberately built without ISO_FLASH_ENABLE_WRITES. Its StartWrite method fails closed with WRITE_DISABLED. A separate CI-only service executable enables the write path and loop-device test exception. That exception is limited to file-backed loop devices and is not compiled into the package.

## D-Bus request flow

1. The GUI opens the ISO read-only and passes the already-open descriptor through a Unix FD list. It sends the selected target's major/minor numbers, never an arbitrary path.
2. The service validates the received descriptor as a regular, non-empty, read-only image and computes SHA-256 from the same descriptor.
3. The service identifies the invocation's unique D-Bus sender. It obtains UID and PID from the bus daemon, verifies the live process UID and process start-time snapshot, and uses a system-bus-name Polkit subject for the fixed org.iso_flash.write action.
4. Caller identity is queried again after authorization. UID, PID, and process start time must still match.
5. Only after authorization does the service resolve the target major/minor through sysfs, validate the whole USB device and safety metadata, and open its own writable descriptor with fixed restrictive flags.
6. The writable descriptor is checked against the expected block-device identity, capacity, read-only state, and sysfs identity. The full DeviceGuard is repeated before the first write.
7. A move-only job owns both the ISO descriptor and writable target descriptor. The job registry binds it to the D-Bus sender's unique name.
8. The owner can call CancelWrite. A different client cannot cancel the job. The service watches NameOwnerChanged and requests cancellation when the owner disconnects.
9. The service emits owner-directed Progress and Finished signals. The GUI displays progress, terminal state, and errors.
10. The worker writes bounded chunks, handles short writes and EINTR, syncs with fsync, then hashes the written byte range and compares it with the source ISO SHA-256.

## Cancellation and failure semantics

Cancellation prevents additional writes from being scheduled and waits for an in-flight write operation to return. The service makes a best-effort sync of partial data. Cancellation is not rollback: any bytes already written remain on the target, which may be unusable.

Device-loss and I/O errors are terminal. The service does not reopen a pathname or switch to a replacement device. Failed, cancelled, or disconnected jobs may leave a partial image and report the transferred byte count.

## Verification performed by CI

The workflow:

- Builds the Qt GUI, engine, and tests with warnings enabled.
- Runs the C++ regression suite and D-Bus/systemd/Polkit contract checks.
- Builds iso-flash-engine and iso-flash Debian packages.
- Installs, purges, verifies removal, and reinstalls both packages.
- Starts the real system D-Bus and Polkit services and verifies that an unprivileged caller is denied or challenged.
- Installs a temporary CI-only Polkit rule for the runner account, then exercises the enabled service against a disposable file-backed loop device.
- Tests D-Bus Unix-FD transfer, owner-only cancellation, caller-disconnect cancellation, progress/completion signals, fsync, and SHA-256 read-back. A test-only fault injection forces EIO after the first 1 MiB written to a disposable loop device; the service must classify the loss as ENODEV, report the exact partial byte count, and preserve a matching source prefix.

The CI test exception and injected EIO path are not part of the production package. CI must never write to host physical disks.

## Remaining gates before enabling production writes

- Perform a manual GUI smoke test in a real desktop session, including service-unavailable, Polkit denial, progress, cancellation, and terminal-error display.
- Review the full destructive path and production Polkit/systemd policy before defining a production build with ISO_FLASH_ENABLE_WRITES.
- Run final CI on the exact release commit. Do not create a release tag while any required job is failing.

## Explicit limitations

- Only hybrid Linux ISO raw imaging is supported.
- Windows installer ISOs need a separate preparation flow (UEFI/FAT32 boot partition and NTFS handling for oversized install.wim).
- Loop-device tests prove the software path against a disposable block device; they do not prove compatibility with every USB controller, flash drive, or firmware.
- The current packaged service is read-only by design until the release gates above are complete.
