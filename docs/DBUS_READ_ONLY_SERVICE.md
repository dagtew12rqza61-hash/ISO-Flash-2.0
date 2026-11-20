# Read-only D-Bus inspection service

The first system service milestone exports only
`org.iso_flash.Manager1.InspectTarget(u major, u minor) -> (b accepted, t capacity_bytes, s reason)`.

The service resolves the supplied major/minor pair as
`/dev/block/MAJOR:MINOR` and calls the same read-only target inspection path
used by the engine. The opened descriptor's device number must match the exact
requested major/minor pair; a substituted or mismatched device node is rejected.
The service never accepts an arbitrary device path from the caller. The D-Bus
method is intentionally read-only and does not authorize or perform writes.

The service runs as root because device-node access and complete system
metadata inspection must be consistent across desktop sessions. Its systemd
unit applies a restrictive filesystem and capability boundary. It has no
write method, no shell execution, no client-supplied executable, and no writable
device descriptor.

## Current boundary

- Implemented: system-bus activation, typed read-only inspection, major/minor
  device selection, a hardened systemd unit, and read-only inspection bound to
  the opened block-device descriptor. The reusable target-admission helper now
  runs the full DeviceGuard before opening, after reading the opened descriptor,
  and during final snapshot revalidation. A contract test enforces these checks.
- Not implemented: Polkit write authorization, image-FD transfer through D-Bus,
  writable target opening, service-integrated asynchronous write jobs,
  cancellation, progress, and read-back verification. The reusable admission
  helper still opens targets read-only and is not a write authorization boundary.
- The broader method contract in [DBUS_INTERFACE.xml](DBUS_INTERFACE.xml)
  remains a future protocol specification. The service does not export
  `StartWrite` or `CancelWrite`.

Do not interpret a successful inspection as authorization to write. Physical
writes remain disabled.
