# Privileged write architecture (design, not implemented)

## Security boundary

The Qt GUI must remain unprivileged. It must never run as root and must never invoke a shell command containing a device path. A GUI-side device scan is advisory only; every decision is repeated by the privileged service.

The preferred boundary is a small system D-Bus service, not a setuid binary and not a GUI launched through pkexec. The service owns the device file descriptor for the complete operation. Polkit authorizes the D-Bus caller for each write request.

## D-Bus interface contract

The initial typed interface is described in [DBUS_INTERFACE.xml](DBUS_INTERFACE.xml). It defines read-only target inspection, a write request carrying an image Unix-FD, job cancellation, and progress/completion signals. The running implementation exports read-only `InspectTarget` and `ValidateImage` methods plus `CheckWriteAuthorization`. The authorization method is non-destructive: it obtains the caller's unique D-Bus name, asks the bus daemon for its UID and PID, binds those credentials to the live `/proc` UID and process start-time snapshot, and calls Polkit with a `system-bus-name` subject for `org.iso_flash.write`. It runs asynchronously, permits interactive administrator authentication, is not cached, and cancels its request when the caller disconnects. It does not open a target or start a write job. The write, cancellation, and progress entries in the XML remain future protocol specifications. `StartWrite` must not be exposed until the service-side Polkit check, descriptor-bound safety checks, and write-job ownership are implemented.

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
- Validate the image descriptor as a regular file, capture its device/inode/size/mtime/ctime snapshot, hash it, and reject an empty or changing source. The initial read-only descriptor validation and metadata recheck helper are implemented. `engine/src/image_hash.hpp` now computes SHA-256 from that same open descriptor using bounded 1 MiB `pread` chunks, without changing the shared file offset. It handles EINTR, short reads, premature EOF, read errors, cancellation between reads, and offset-range overflow. It compares source snapshots before and after hashing and rejects metadata-visible mutation. Tests cover the known SHA-256 of `abc`, interrupted/short reads, cancellation, read errors, premature EOF, and mutation during hashing. Hashing remains independent of D-Bus and physical I/O. `engine/src/image_validation.hpp` now wraps the SHA-256 helper in a synchronized validation result: it compares against the snapshot captured at admission, maps cancellation and source mutation to explicit error codes, and retains the computed digest for the subsequent worker operation. A worker-level test proves that the operation sees the completed digest and that a source changed after admission is rejected before the operation callback runs. This is still a reusable in-process primitive, not a service-connected write request; it does not freeze the source against a hostile writer that can modify and restore content and timestamps, and it does not provide read-back verification.
- Use bounded write loops, handle short writes and EINTR, sync at defined checkpoints, and verify read-back data.
- Treat ENODEV/ENXIO during open, fstat, ioctl, write, or sync as a terminal device-loss event; stop scheduling writes, never retry against a newly resolved pathname, preserve the original descriptor identity in the job result, and report that the target may contain a partially written image.
- Keep all destructive operations in the service; expose only typed D-Bus methods and progress signals.
- Test with disposable loop devices and fault injection. CI must never write to host physical disks.

## Polkit and service packaging

The service should be installed root-owned and non-writable by unprivileged users. The D-Bus system service file should start a dedicated executable with a fixed path and restrictive systemd sandboxing where supported. The Polkit policy should define only `org.iso_flash.write`, default to authorization for an active local session, and require authentication. No wildcard actions, arbitrary command execution, user-provided helper paths, or environment-controlled executable paths are allowed.

## Polkit authorization policy (packaged, not yet enforced by a write method)

The engine package now installs `packaging/polkit/org.iso_flash.policy` as
`/usr/share/polkit-1/actions/org.iso_flash.policy`. It defines exactly one
action, `org.iso_flash.write`. Any subject is denied by default; active and
inactive sessions require administrator authentication (`auth_admin`). The
policy is intentionally restrictive and is covered by a CMake contract test
and the Debian package smoke test.

This policy file alone does not authorize or enable writes. The running D-Bus
service still exports no `StartWrite` method and does not call Polkit. When
that method is implemented, authorization must be checked asynchronously for
every request using the invocation's unique D-Bus sender as a
`PolkitSystemBusName` subject. Do not trust a UID, username, PID, or session
identifier supplied as a method argument. Use only the fixed action ID
`org.iso_flash.write`, allow interactive authentication, bind the check to
the request's cancellation lifecycle, and fail closed on Polkit errors,
missing sender identity, timeout, or caller disconnect. Do not cache approval
in the GUI or reuse an earlier request's authorization. Revalidate the target
after authorization because the device may change while the authentication
dialog is open.

## Writable target descriptor admission (design only)

The current `OpenedTarget` helper remains read-only. The future writer must
not change it into a permissive path-based open. Add a separate move-only
writable-target type whose descriptor is created and owned only by the root
D-Bus service. The client sends major/minor numbers, never a path.

Resolve the requested identity through `/sys/dev/block/MAJOR:MINOR`, obtain
the kernel-reported `DEVNAME` from that exact sysfs device's uevent metadata,
and reject empty names, slash-containing names, dot components, or any value
that is not a single device-node basename. Construct only `/dev/DEVNAME`;
never accept a client-provided path or follow `/dev/disk/by-*` aliases. Use
`lstat` to require a real block-device node (not a symlink), then open it with
`O_RDWR | O_CLOEXEC | O_EXCL | O_NOFOLLOW | O_NONBLOCK`. Retry only
interrupted opens. Do not fall back to read-write opens without `O_EXCL` or
`O_NOFOLLOW`.

Immediately after open, use `fstat` and require `S_ISBLK` and an exact
`st_rdev == makedev(requested_major, requested_minor)` match. Query
`BLKGETSIZE64` and `BLKROGET` through that same descriptor; require nonzero
capacity and writable state. Resolve the sysfs identity again and compare its
canonical device path and capacity with the admission snapshot. Run the complete
DeviceGuard against the descriptor's device number after open and again
immediately before the first write. Keep the same descriptor for the entire
job; never close it and reopen a pathname between validation and I/O. Treat
`O_EXCL` as an additional kernel-level exclusion check, not a replacement for
mount, swap, holder, topology, or identity validation.

A path mismatch, symlink, changed major/minor, changed capacity, read-only
transition, newly mounted descendant, active swap, new holder, lost USB
ancestry, or any uninspectable safety metadata must reject admission. Once a
job starts, any descriptor error or device loss is terminal; never resolve the
old major/minor again and continue against a replacement device. No fallback
to a different device is permitted.

## Service privilege boundary

The service remains a fixed root-owned system D-Bus executable; the GUI remains
unprivileged. The systemd unit now additionally sets a restrictive umask,
private keyring, native syscall architecture, and disables namespace creation
and realtime scheduling. It retains `PrivateDevices=false` because the service
must inspect actual host block devices, and it must not use `ProcSubset=pid`
because DeviceGuard reads `/proc/self/mountinfo` and `/proc/swaps`. Its
capability bounding set remains narrow; do not add `CAP_SYS_ADMIN`,
`CAP_SYS_RAWIO`, or broad capabilities without a demonstrated kernel
requirement and a separate security review. Device access must remain through
the fixed service code path, never through a shell or user-selected executable.

These hardening directives and the Polkit policy are tested in CI. They are
defense-in-depth configuration only; they do not make the currently read-only
service capable of writing.

## Writable target admission primitive (implemented, not connected)

A separate move-only `WritableTarget` primitive now exists in
`engine/src/writable_target.hpp`; the existing read-only `OpenedTarget`
path is unchanged. The new helper resolves a requested major/minor identity
through `/sys/dev/block`, verifies the sysfs `dev` field, extracts the
kernel-provided `DEVNAME`, and accepts only a safe single-component device
node basename. It constructs `/dev/DEVNAME` itself and uses `lstat` to reject
symlinks and non-block nodes before opening.

The writable open flags are fixed in code:
`O_RDWR | O_CLOEXEC | O_EXCL | O_NOFOLLOW | O_NONBLOCK`. The helper retries
only `EINTR`, owns the descriptor through RAII, verifies `fstat` block type
and exact `st_rdev`, checks `BLKGETSIZE64` and `BLKROGET`, compares the
canonical sysfs identity before and after opening, and runs the full
DeviceGuard multiple times. A separate
`revalidate_writable_target_before_first_write` performs descriptor,
sysfs-node, path, capacity, read-only, and full DeviceGuard checks at the
pre-write boundary.

The unit test currently covers safe/unsafe kernel node names and the mandatory
exclusive no-follow open flags. CI does not open a real writable block device
through this helper and no caller invokes it. It is an admission primitive
only: it does not authorize a user, write bytes, create a D-Bus job, or enable
the CLI. Before any integration, it needs fault-injection coverage for
sysfs/path replacement, ioctl failures, device loss, mount/swap races, and
the exact first-write boundary. Physical writing remains disabled.

## Current implementation status

The read-only D-Bus service and systemd activation are implemented. Read-only open and descriptor-inspection failures now provide differentiated diagnostics for common errors such as ENODEV, ENXIO, EIO, EBUSY, and permission denial; interrupted opens are retried. This improves diagnosis only and does not implement a hot-unplug-safe write job. The Polkit policy is now enforced by the non-destructive `CheckWriteAuthorization` D-Bus method and tested against a synthetic PolicyKit authority in CI. The descriptor-passing write request, writable device open, D-Bus job integration, and physical write path are not implemented. The asynchronous worker and in-memory job registry primitives exist, but are not connected to the service. The current engine supports read-only target inspection; `--write` remains deliberately disabled.

## Write-job state model (implemented, not connected to a writer)

The engine now contains a pure, header-only state transition model in `engine/src/write_job_state.hpp`. Its states are `Queued`, `Validating`, `Writing`, `Cancelling`, and terminal states `Completed`, `Failed`, `DeviceLost`, and `Cancelled`. Invalid transitions are rejected without mutating the current state. Cancellation while queued terminates immediately; cancellation during validation or writing enters `Cancelling` and must be finalized by the worker only after it has stopped scheduling writes and handled any in-flight operation. Device loss is terminal.

This model is unit-tested, but it is not yet an asynchronous job manager, does not own descriptors, and is not exposed over D-Bus. No physical write operation is enabled. A cancelled or device-lost job must explicitly report possible partial target contents; cancellation is not an undo operation.

The header-only `WriteJob` model in `engine/src/write_job.hpp` now owns the image and target descriptors through `UniqueFd` for the lifetime of the job object. It is move-only, exposes descriptor values without reopening paths, and has a unit test for ownership transfer and deterministic close on destruction. This is an ownership primitive only: it does not open devices, schedule work, or perform writes.

## Write I/O primitive (implemented, not connected to a device writer)

The header-only `engine/src/write_io.hpp` provides an injectable bounded write loop. It handles short writes, retries `EINTR`, stops when cancellation is requested between in-flight write calls, rejects zero-progress writes, preserves the underlying error code, and rejects negative or overflowing offsets. Its tests inject every I/O outcome and use in-memory callbacks; they do not open or write to block devices. A cancelled or failed result includes the number of bytes already transferred so the caller can report possible partial target contents.

The header-only `WriteProgress` model in `engine/src/write_progress.hpp` stores a fixed nonzero total and a monotonic completed-byte count. It rejects regressions, values beyond the declared total, and zero-length jobs; remaining-byte calculations stay within the unsigned counter range. Its tests cover partial progress, completion, invalid updates, and maximum-width counters. The model is not yet connected to a worker, D-Bus signal, or GUI progress display.

The asynchronous `WriteWorker` primitive in `engine/src/write_worker.hpp` owns a move-only `WriteJob` and runs an injected validation callback followed by an injected operation on a dedicated worker thread. The new `ImageValidation` helper can be used directly as that validation callback to hash the already-open image descriptor against its admission-time snapshot and retain the digest for the operation callback. The validation callback receives a cancellation query, allowing long-running checks such as SHA-256 hashing to stop cooperatively; a validation result returned after cancellation is finalized as `Cancelled`, not misreported as a validation failure. Cancellation is also checked after validation and can prevent the operation from starting. It exposes synchronized progress snapshots, cancellation requests, and deterministic join-on-destruction. It classifies ENODEV, ENXIO, and ENOMEDIUM from validation or I/O as terminal device loss, preserves ordinary I/O errors after partial progress, rejects impossible byte counts, handles thread-start failures, and converts callback exceptions into a failed job. Tests use pipe descriptors and injected callbacks only; they do not access block devices. This is orchestration infrastructure, not a production writer: no real block-device operation, D-Bus job integration, or GUI progress integration is connected.

The synchronized `WriteJobControl` model in `engine/src/write_job_control.hpp` provides a mutex-protected state/progress snapshot used by the asynchronous worker. It serializes cancellation against progress and terminal transitions, refuses completion until all declared bytes are accounted for, and records an error number for failed or device-lost jobs. Its tests exercise concurrent cancellation requests and lifecycle invariants. It remains an in-process control primitive and is not exposed through D-Bus.

This worker and registry are not called by the current CLI, D-Bus service, or GUI. They do not make physical writing available.


## Asynchronous job registry (implemented, not exposed to D-Bus)

The header-only `WriteJobRegistry` in `engine/src/write_job_registry.hpp` owns workers by stable nonzero job ID. It rejects duplicate IDs and admission after shutdown, provides synchronized snapshot lookup and cancellation forwarding, removes terminal jobs only after moving ownership out from under its mutex, and cancels/joins all remaining workers during shutdown. Worker threads are joined without holding the registry lock, preventing shutdown or reaping from blocking registry lookups while a worker exits. Unit tests use pipe descriptors and injected callbacks only. The registry is not connected to D-Bus, the GUI, Polkit, or any physical write operation; `--write` remains disabled.


## ISO source descriptor validation (implemented, not connected to writing)

`engine/src/image_source.hpp` provides read-only ISO descriptor validation. It opens with `O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK`, retries interrupted opens and `fstat`, requires a non-empty regular file, and captures device number, inode, size, modification time, and change time. The new move-only `OpenedImage` binds the owned descriptor and admission-time snapshot; `WriteJob` can adopt both together and retains the snapshot for its entire lifetime. A regression test checks descriptor transfer and snapshot preservation. A later descriptor snapshot can be compared to detect source replacement or metadata-visible mutation. The tests cover valid files, empty files, non-regular descriptors, symlink rejection, EINTR retry, and source mutation. The descriptor validation helper is paired with `engine/src/image_hash.hpp`, which computes SHA-256 from the same open descriptor in bounded 1 MiB `pread` chunks and rechecks metadata before and after hashing. Hashing is independently tested but is not yet invoked by the D-Bus service or a real write request. Metadata checks do not freeze a file against a hostile writer that can alter and restore content and timestamps; read-back verification is also not implemented. Physical writing remains disabled.


## Read-only target admission descriptor (implemented, not a write authorization path)

The move-only `OpenedTarget` in `engine/src/target_source.hpp` owns a read-only block-device descriptor together with the captured major/minor identity, capacity, and read-only state. Its admission helper requires an expected nonzero device identity, runs the full `validate_target_device` DeviceGuard before opening, opens read-only, compares the descriptor's `st_rdev` to the expected identity, reruns DeviceGuard against that descriptor identity, rechecks the path and descriptor snapshot, and performs a final DeviceGuard pass before transferring ownership. `WriteJob` can adopt both an `OpenedImage` and an `OpenedTarget`, retaining both admission snapshots with the exact descriptors.

The regression test verifies move-only ownership transfer using pipe descriptors and synthetic metadata; it does not open or write a physical or loop block device. A separate CI source-contract test asserts that the three DeviceGuard admission calls remain present. This helper is not yet called by the D-Bus service and does not open a writable descriptor. A future privileged service must still obtain per-request Polkit authorization, open the target itself with the final writable flags, validate that exact writable descriptor, and keep it bound to the job. Physical writing remains disabled.


## D-Bus caller identity and Polkit enforcement (implemented, no write endpoint)

`engine/src/polkit_authorization.hpp` obtains the caller PID and UID from the
system bus using the invocation's unique sender name. It independently checks
the real UID in `/proc/PID/status`, captures process start ticks from
`/proc/PID/stat`, and repeats the bus credential and process-start checks
after the Polkit round trip. Polkit receives a `system-bus-name` subject
containing only the unique D-Bus sender, the fixed action
`org.iso_flash.write`, and `ALLOW_USER_INTERACTION`. Authorization is
per-call and never cached. The service runs the check in a worker task and
watches `NameOwnerChanged`; disconnect cancels the outstanding call. Any
missing credential, UID mismatch, process replacement, Polkit denial,
challenge not satisfied, service error, or cancellation is a fail-closed
non-authorized result.

The service exports `CheckWriteAuthorization` solely to exercise this boundary.
It returns the observed UID, PID, process start ticks, authorization decision,
and diagnostic reason. It does not accept a device, open a block device, create
a job, or write data. CI uses a synthetic PolicyKit authority on an isolated
D-Bus daemon to test allow, deny, authority failure, subject type, action ID,
interactive flag, and caller identity. Physical writing remains disabled.
