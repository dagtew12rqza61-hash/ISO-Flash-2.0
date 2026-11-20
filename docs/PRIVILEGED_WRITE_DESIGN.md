# Privileged write architecture (design, not implemented)

## Security boundary

The Qt GUI must remain unprivileged. It must never run as root and must never invoke a shell command containing a device path. A GUI-side device scan is advisory only; every decision is repeated by the privileged service.

The preferred boundary is a small system D-Bus service, not a setuid binary and not a GUI launched through pkexec. The service owns the device file descriptor for the complete operation. Polkit authorizes the D-Bus caller for each write request.

## Proposed request protocol

1. The GUI opens the selected ISO read-only and passes the already-open image descriptor as a D-Bus Unix file descriptor. It sends the selected block device's major/minor numbers, not an arbitrary path.
2. The service resolves the major/minor pair through `/sys/dev/block/MAJOR:MINOR`, verifies that it is a whole USB block device, and checks system-root ancestry, holders/slaves, all mounted descendants, swap, read-only state, and capacity.
3. The service asks Polkit to authorize the D-Bus message sender for the dedicated action `org.iso_flash.write`, with interactive authorization allowed. Authorization is per request and is not cached by the GUI.
4. After authorization, the service resolves the device again and opens `/dev/block/MAJOR:MINOR` itself using `O_RDWR | O_CLOEXEC | O_EXCL | O_NOFOLLOW | O_NONBLOCK`. It verifies with `fstat` that the descriptor is a block device and that `st_rdev` matches the requested major/minor pair.
5. With that descriptor held open, the service repeats all safety checks against the descriptor's `st_rdev`, queries `BLKGETSIZE64` and `BLKROGET`, and refuses any mismatch. It does not close and reopen the device by pathname between validation and writing.
6. Only after all checks pass may the service create a write job. The write job owns the exact device descriptor and image descriptor until completion or cancellation. The GUI receives a job identifier and progress events; it never receives a writable device descriptor.
7. Cancellation stops new writes, waits for the in-flight write to finish, calls `fsync` where appropriate, reports the final state, and releases descriptors. Disconnecting the GUI must not silently restart or detach a destructive job.

## Required checks before enabling writes

- Resolve block-device topology in both directions. Reject the system disk and every device that is an ancestor/descendant or holder/slave of it, including device-mapper, RAID, LVM, and partition relationships.
- Decode mountinfo escapes correctly and fail closed if mount or swap state cannot be inspected.
- Verify the device is removable USB mass storage using sysfs transport ancestry, not a substring match alone.
- Compare capacity, read-only state, major/minor identity, and sysfs canonical path before and after opening.
- Re-check mounts, swap, holders, and topology immediately before the first write.
- Validate the image descriptor as a regular file, capture its size, hash it, and reject an empty or changing source.
- Use bounded write loops, handle short writes and EINTR, sync at defined checkpoints, and verify read-back data.
- Keep all destructive operations in the service; expose only typed D-Bus methods and progress signals.
- Test with disposable loop devices and fault injection. CI must never write to host physical disks.

## Polkit and service packaging

The service should be installed root-owned and non-writable by unprivileged users. The D-Bus system service file should start a dedicated executable with a fixed path and restrictive systemd sandboxing where supported. The Polkit policy should define only `org.iso_flash.write`, default to authorization for an active local session, and require authentication. No wildcard actions, arbitrary command execution, user-provided helper paths, or environment-controlled executable paths are allowed.

## Current implementation status

This document is an architecture specification only. The D-Bus service, Polkit policy, descriptor-passing protocol, writable device open, and physical write path are not implemented. The current engine supports read-only target inspection; `--write` remains deliberately disabled.
