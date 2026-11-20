# Read-only D-Bus service

The system service exports two read-only methods:

- `InspectTarget(u major, u minor) -> (b accepted, t capacity_bytes, s reason)`
- `ValidateImage(h image_fd) -> (b accepted, t size_bytes, s sha256, s reason)`

`InspectTarget` resolves the supplied major/minor pair as
`/dev/block/MAJOR:MINOR` and calls the same read-only target inspection path
used by the engine. The opened descriptor's device number must match the exact
requested major/minor pair; a substituted or mismatched device node is rejected.
The service never accepts an arbitrary device path from the caller.

`ValidateImage` receives an already-open file descriptor through D-Bus Unix-FD
passing. It duplicates the received descriptor into service-owned RAII
ownership, requires the descriptor access mode to be read-only, verifies that
it refers to a nonempty regular file, and computes SHA-256 through bounded positional reads on a GLib worker thread,
keeping the D-Bus main loop responsive. The service admits at most two concurrent
image validations through a move-only RAII capacity lease. Excess requests are
rejected with a retryable busy response. The lease is released automatically
when the asynchronous task data is destroyed, including completion and task
setup failure paths, preventing unbounded hashing tasks from exhausting the
privileged service. The limiter has a dedicated concurrency and lease-lifecycle
unit test.
No caller-supplied path is opened.
The method does not write to the image or any target device.

The service runs as root because device-node access and complete system
metadata inspection must be consistent across desktop sessions. Its systemd
unit applies a restrictive filesystem and capability boundary. It has no
write method, no shell execution, no client-supplied executable, and no
writable device descriptor.

## Current boundary

- Implemented: system-bus activation, typed read-only target inspection,
  descriptor-bound image validation, Unix-FD reception, read-only access-mode
  enforcement, SHA-256 validation, and a hardened systemd unit.
- Not implemented: Polkit write authorization, writable target opening,
  service-integrated asynchronous write jobs, cancellation, progress, and
  read-back verification.
- The broader method contract in [DBUS_INTERFACE.xml](DBUS_INTERFACE.xml)
  remains a future protocol specification. The service does not export
  `StartWrite` or `CancelWrite`.

A successful image validation is not authorization to write. Physical writes
remain disabled.
