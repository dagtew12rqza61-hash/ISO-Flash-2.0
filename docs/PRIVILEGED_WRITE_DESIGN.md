# Privileged write architecture (design, not implemented)

## Security boundary

The Qt GUI must remain unprivileged. It must never run as root and must never invoke a shell command containing a device path. A GUI-side device scan is advisory only; every decision is repeated by the privileged service.

The preferred boundary is a small system D-Bus service, not a setuid binary and not a GUI launched through pkexec. The service owns the device file descriptor for the complete operation. Polkit authorizes the D-Bus caller for each write request.

## D-Bus interface contract

The initial typed interface is described in [DBUS_INTERFACE.xml](DBUS_INTERFACE.xml). It defines read-only target inspection, a write request carrying an image Unix-FD, job cancellation, and progress/completion signals. The running implementation currently exports only the read-only `InspectTarget` method documented in [DBUS_READ_ONLY_SERVICE.md](DBUS_READ_ONLY_SERVICE.md). The write, cancellation, and progress entries in the XML remain future protocol specifications. `StartWrite` must not be exposed until the service-side Polkit check, descriptor-bound safety checks, and write-job ownership are implemented.

## Proposed request protocol

1. The GUI opens the selected ISO read-only and passes the already-open image descriptor as a D-Bus Unix file descriptor. It sends the selected block device's major/minor numbers, not an arbitrary path.
2. The service resolves the major/minor pair through `/sys/dev/block/MAJOR:MINOR`, verifies that it is a whole USB block device, and checks system-root ancestry, holders/slaves, all mounted descendants, swap, read-only state, and capacity.
3. The service asks Polkit to authorize the D-Bus message sender for the dedicated action `org.iso_flash.write`, with interactive authorization allowed. Authorization is per request and is not cached by the GUI.
4. After authorization, the service resolves the device again and opens `/dev/block/MAJOR:MINOR` itself using `O_RDWR | O_CLOEXEC | O_EXCL | O_NOFOLLOW | O_NONBLOCK`. It verifies with `fstat` that the descriptor is a block device and that `st_rdev` matches the requested major/minor pair.
5. With that descriptor held open, the service repeats all safety checks against the descriptor's `st_rdev`, queries `BLKGETSIZE64` and `BLKROGET`, and refuses any mismatch. It does not close and reopen the device by pathname between validation and writing.
6. Only after all checks pass may the service create a write job. The write job owns the exact device descriptor and image descriptor until completion or cancellation. The GUI receives a job identifier and progress events; it never receives a writable device descriptor.
7. Cancellation prevents scheduling additional writes and waits for any in-flight write to return before finalizing the job. It cannot roll back bytes already written: a cancelled operation must report that the target may contain a partial, non-bootable image. Syncing is best-effort and must not be represented as rollback. Disconnecting the GUI must not silently restart or detach a destructive job.

## Required checks before enabling writes

- Resolve block-device topology in both directions. Reject the system disk and every device that is an ancestor/descendant or holder/slave of it, including device-mapper, RAID, LVM, and partition relationships.
- Decode mountinfo escapes correctly and fail closed if mount or swap state cannot be inspected.
- Verify the device is removable USB mass storage using sysfs transport ancestry, not a substring match alone.
- Compare capacity, read-only state, major/minor identity, and sysfs canonical path before and after opening.
- Re-check mounts, swap, holders, and topology immediately before the first write.
- Validate the image descriptor as a regular file, capture its size, hash it, and reject an empty or changing source.
- Use bounded write loops, handle short writes and EINTR, sync at defined checkpoints, and verify read-back data.
- Treat ENODEV/ENXIO during open, fstat, ioctl, write, or sync as a terminal device-loss event; stop scheduling writes, never retry against a newly resolved pathname, preserve the original descriptor identity in the job result, and report that the target may contain a partially written image.
- Keep all destructive operations in the service; expose only typed D-Bus methods and progress signals.
- Test with disposable loop devices and fault injection. CI must never write to host physical disks.

## Polkit and service packaging

The service should be installed root-owned and non-writable by unprivileged users. The D-Bus system service file should start a dedicated executable with a fixed path and restrictive systemd sandboxing where supported. The Polkit policy should define only `org.iso_flash.write`, default to authorization for an active local session, and require authentication. No wildcard actions, arbitrary command execution, user-provided helper paths, or environment-controlled executable paths are allowed.

## Current implementation status

The read-only D-Bus service and systemd activation are implemented. Read-only open and descriptor-inspection failures now provide differentiated diagnostics for common errors such as ENODEV, ENXIO, EIO, EBUSY, and permission denial; interrupted opens are retried. This improves diagnosis only and does not implement a hot-unplug-safe write job. The Polkit write policy, descriptor-passing write request, writable device open, asynchronous job manager, and physical write path are not implemented. The current engine supports read-only target inspection; `--write` remains deliberately disabled.

## Write-job state model (implemented, not connected to a writer)

The engine now contains a pure, header-only state transition model in `engine/src/write_job_state.hpp`. Its states are `Queued`, `Validating`, `Writing`, `Cancelling`, and terminal states `Completed`, `Failed`, `DeviceLost`, and `Cancelled`. Invalid transitions are rejected without mutating the current state. Cancellation while queued terminates immediately; cancellation during validation or writing enters `Cancelling` and must be finalized by the worker only after it has stopped scheduling writes and handled any in-flight operation. Device loss is terminal.

This model is unit-tested, but it is not yet an asynchronous job manager, does not own descriptors, and is not exposed over D-Bus. No physical write operation is enabled. A cancelled or device-lost job must explicitly report possible partial target contents; cancellation is not an undo operation.

The header-only `WriteJob` model in `engine/src/write_job.hpp` now owns the image and target descriptors through `UniqueFd` for the lifetime of the job object. It is move-only, exposes descriptor values without reopening paths, and has a unit test for ownership transfer and deterministic close on destruction. This is an ownership primitive only: it does not open devices, schedule work, or perform writes.

## Write I/O primitive (implemented, not connected to a device writer)

The header-only `engine/src/write_io.hpp` provides an injectable bounded write loop. It handles short writes, retries `EINTR`, stops when cancellation is requested between in-flight write calls, rejects zero-progress writes, preserves the underlying error code, and rejects negative or overflowing offsets. Its tests inject every I/O outcome and use in-memory callbacks; they do not open or write to block devices. A cancelled or failed result includes the number of bytes already transferred so the caller can report possible partial target contents.

This helper is not called by the current CLI, D-Bus service, or GUI. It does not make physical writing available.
