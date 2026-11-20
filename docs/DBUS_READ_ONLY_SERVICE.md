# D-Bus service and privileged write path

The system-bus service exports the complete ISO Flash manager interface. The Qt GUI remains unprivileged; it never opens a writable block device and never invokes a privileged shell command.

## Methods

- InspectTarget(u major, u minor) -> (b accepted, t capacity_bytes, s reason)
- CheckWriteAuthorization() -> (b authorized, u uid, u pid, t process_start_ticks, s reason)
- ValidateImage(h image_fd) -> (b accepted, t size_bytes, s sha256, s reason)
- StartWrite(h image_fd, u major, u minor) -> (b accepted, t job_id, s reason)
- CancelWrite(t job_id) -> (b accepted, s reason)

## Signals

- Progress(t job_id, s state, t completed_bytes, t total_bytes)
- Finished(t job_id, s state, t completed_bytes, t total_bytes, i error_number, s message)

Signals are directed to the unique D-Bus name that owns the corresponding job. A different client cannot cancel that job.

## StartWrite security flow

1. The GUI opens the ISO as a regular read-only file and passes that descriptor through D-Bus Unix-FD passing. The request includes only the selected target's major/minor numbers, not an arbitrary path.
2. The service duplicates and owns the received descriptor, verifies read-only access, regular-file type, nonzero size, source snapshot, and SHA-256.
3. The service derives the caller from the invocation's unique D-Bus sender. It queries UID and PID from the bus daemon, verifies the live process identity and process start time, and authorizes the system-bus-name subject through the fixed Polkit action org.iso_flash.write.
4. Caller UID, PID, and process start time are checked again after authorization. Missing sender, disconnect, cancellation, timeout, or Polkit error fails closed.
5. Only after successful authorization does the service resolve major/minor through sysfs, verify USB transport and full DeviceGuard constraints, open the block device exclusively with fixed flags, and bind the writable descriptor to the exact device identity and capacity.
6. The service repeats target validation at the first-write boundary. The job owns both descriptors and is registered under the caller's unique D-Bus name.
7. CancelWrite is owner-only. NameOwnerChanged watches the owner; disconnect requests cancellation. The worker tracks partial progress, handles short writes and EINTR, calls fsync, and reads the written range back for SHA-256 comparison.
8. Device loss, I/O errors, cancellation, and integrity mismatch are terminal. Bytes already written cannot be rolled back; the target may be left partially imaged.

## Image validation and concurrency

ValidateImage uses the same received-descriptor validation path but does not open or modify any target. Hashing is asynchronous, bounded, and limited to two concurrent validations. A caller disconnect cancels its outstanding hash operation. Validation errors and exceptions are returned as failed D-Bus replies.

## Privilege boundary

The system service runs as root with a hardened systemd unit. The normal packaged service enables the Polkit-gated write method; the GUI remains unprivileged. The D-Bus policy permits only the service account to own the manager name and limits service-originated signals to the manager interface.

The CI-only service executable adds a test exception for file-backed loop devices and deterministic device-loss injection. Those test hooks are not compiled into the packaged service. CI writes only to disposable loop devices, never physical media.

## Limitations and release state

- Raw imaging supports hybrid Linux ISO images. Windows installer ISO preparation requires a separate FAT32/NTFS-aware flow.
- Loop-device integration tests validate the software path but do not certify every USB controller or flash drive.
- The project still requires a manual desktop-session GUI smoke test and security review before publishing a GitHub Release.
