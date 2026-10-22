# OresIoT

Connected-device protocols and gateway services for **Oreslang**.

> **Status: bootstrap.** This repository ships a small, independently testable binary framing reference with strict input validation. It does **not** yet implement MQTT, CoAP, provisioning, OTA, network actor transport, authentication, or an Oreslang production runtime.

## Initial deliverable

- A versioned, length-bounded frame codec suitable for portable native interoperability tests.
- Exact-length parsing that rejects malformed versions, kinds, sizes and trailing bytes.
- A runnable example and CI checks.
- Security and architecture contracts for eventual MQTT/CoAP, TLS, identity, and signed OTA integration.

The frame codec is **not** a secure transport protocol. Security requires an authenticated channel, device identity, authorization, replay mitigation and rate limiting.

## Quick start

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

See `docs/ARCHITECTURE.md` and `docs/SECURITY.md`.

Call `ores_iot_stream_finish` at end of input to reject a truncated header or payload. It returns `ORES_IOT_OK` only at a frame boundary and closes the stream until reinitialized. This is framing validation; it does not authenticate a peer.

## Experimental replay-admission primitive

`include/ores_iot_replay.h` defines a bounded, allocation-free 64-sequence
sliding window. It rejects duplicates, zero, out-of-window messages, and
implicit uint64 counter wraparound. Host CTest exercises 40,000 differential
operations against an independent per-sequence reference model.

**Not an authenticated protocol:** the caller MUST first authenticate the
frame including sequence, peer identity, key epoch and payload. Each
peer/key epoch needs its own serialized window. An accepted checkpoint must
be durably committed **before** executing the action; otherwise a reboot
or power loss can permit replay. Do not persist raw window structure bytes
as a protocol or disk schema; encode/decode versioned integer fields.
`init()` only starts a new trusted identity/epoch, never resumes a device
after a power cycle. The primitive has no crypto, storage, provisioning,
fleet-management, MQTT or CoAP implementation.


### Transactional replay preview and crash-point model

`ores_iot_replay_preview(committed, sequence, next)` proposes a new
64-sequence watermark without changing the committed in-memory state.
The caller must authenticate and serialize the peer/key-epoch stream,
persist a versioned checkpoint durably, then publish the candidate before
executing externally visible work. Do not call `ores_iot_replay_admit`
on the live window as a substitute for durable admission.

`iot_replay_crash_points` is an **in-memory model** of a caller-owned
two-slot checkpoint: it injects failures before/during publication,
rejects a corrupted published snapshot, and demonstrates that replay can
be rejected even when the associated effect was lost. It is not proof
of storage atomicity, real power-cut recovery, MAC/AEAD integrity, or
exactly-once effects. A production storage implementation must provide
fail-closed recovery and verified durability ordering. If effects need
exactly-once semantics, add a separately designed transactional outbox
or idempotent handler with a durable effect record.

### Binary checkpoint codec v1 — experimental host reference

`include/ores_iot_checkpoint.h` and `src/ores_iot_checkpoint.c` now encode
the replay-window state in a **fixed 64-byte little-endian record**, independently
of C struct padding and host endianness. It contains magic/version, a caller-
supplied authenticated peer binding, key epoch, generation counter, replay
window, CRC32C plus complement, and an eight-byte final commit marker.
All-zero commit markers mean **unpublished**. A nonzero invalid commit marker,
a damaged published record, duplicate generations, and wrong identity/epoch
fail closed. If both slots are published, the highest generation wins.

`iot_checkpoint_every_byte_torn_write` enumerates all 57 possible prefix
lengths of a prepared record (0 through 56), all seven partial nonzero
publication markers, and mutations of each of the 64 published bytes. The
test also covers duplicate-generation denial, foreign identity/epoch, invalid
window state, buffer aliasing, version incompatibility, and empty storage.
The codec allocates no heap memory and does not access the filesystem.

**Not a flash/filesystem driver or durability proof.** For real storage,
serialize all admissions for each authenticated peer/key epoch, keep the latest
record intact while updating the older slot, durably invalidate the target
slot's marker, durably write and flush its bytes 0–55, then atomically write
and durably flush the final eight-byte marker. Only after that final barrier
may externally visible effects run. Target hardware/storage **must guarantee**
the atomic-marker and flush-order requirements; otherwise the format is
unsuitable for production. CRC32C is not cryptographic integrity, and two
slots alone do not prevent malicious rollback. A trusted monotonic authority,
MAC/AEAD binding, physical power-cut tests, and an idempotent-effect/outbox
protocol are separate requirements where applicable. A crash after checkpoint
publication but before an effect can lose the effect: no exactly-once guarantee.

### Ordered checkpoint-storage adapter (host reference)

`ores_iot_checkpoint_load` and `ores_iot_checkpoint_commit_sequence`
in `include/ores_iot_checkpoint_store.h` use a caller-owned two-slot
`ores_iot_checkpoint_io` callback interface. The commit operation re-reads
durable state, checks expected generation, previews the authenticated replay
sequence, and writes only the older/inactive slot. Order:

1. Write an **unpublished** zero marker to the inactive slot and flush it.
2. Write the 56-byte versioned record and flush it.
3. Atomically publish the 8-byte marker **durably**.

The `publish_marker` callback MUST guarantee atomic power-loss-safe publication
and durability before returning success. The `flush` callback MUST order and
durably persist prior successful writes. Merely issuing a POSIX `pwrite`, a
file `fsync`, or an ordinary MCU word-store is NOT automatically proof of an
atomic 8-byte crash-safe marker for every filesystem or flash geometry.
Platform teams must validate these properties using their storage hardware,
flash erase rules, sector layout, device write caches and true power-cut tests.
Operations must be serialized per authenticated peer/key epoch; the expected
generation check does not replace external concurrency exclusion.

Any failure after an attempted storage mutation returns `IN_DOUBT`: the final
marker may have been published even if a callback reported failure. Discard
cached state, reload trusted durable checkpoints, and re-evaluate the message.
Uninitialized storage, invalid checkpoints, conflicting generations, and
counter exhaustion fail closed. Trusted provisioning, cryptographic identity
and epoch binding, hardware monotonic rollback protection, and exactly-once
effects remain separate integration requirements.

`iot_checkpoint_ordered_store_failpoints` tests a simulated durable write
buffer, both write barriers, all injected partial-write lengths, the final
ambiguous-commit outcome, stale expected generations, duplicate/stale replay,
generation exhaustion and two consecutive slot rotations. The simulator
is NOT proof of any real storage driver's durability guarantees.

### Linux checkpoint filesystem adapter (host-only reference)

`src/ores_iot_checkpoint_posix.c` and `include/ores_iot_checkpoint_posix.h`
provide a **Linux-only** `ores_iot_checkpoint_io` implementation backed
by two 64-byte slot files in a caller-provisioned, stable, owner-owned
private (0700) directory. This is optional (`ores_iot_checkpoint_posix`
CMake target) and is not linked into the portable IoT frame library.

The adapter takes a nonblocking, advisory exclusive `flock` on the directory
for the lifetime of the store; another cooperative writer cannot open a
second adapter concurrently. Do not fork with an open adapter, rename the
directory during use, or allow another process to mutate its files without
participating in the lock. Files are opened using `openat` and `O_NOFOLLOW`;
unexpected file types, symlinks, hardlinks, truncation and ownership/permission
problems fail closed. On close, staged replay bytes are scrubbed.

Each flush writes a complete 64-byte temporary file using exclusive creation,
fsyncs the file, then atomically renames it over the older slot in the same
directory and fsyncs the directory. Final publication uses **another**
whole-record rename, so the host reference does not depend on an atomic
in-place 8-byte write. Successful `publish_marker` requires that sequence
to complete; an error after rename may still be an `IN_DOUBT` commit.

`iot_posix_fsync_rename_process_crashes` tests reopening after abrupt
child-process termination at each of five callback boundaries, plus death
immediately after successful publication. It also checks the exclusive
writer lease, unsafe permissions, corrupted published files, symlinks,
truncated files and shutdown scrubbing.

**Security and durability limitations:** This is a POSIX filesystem reference
for trusted local Linux host environments, **not proof of actual power-failure
durability**. Rename visibility, file/directory fsync guarantees, storage
writeback caches and failures differ by filesystem, mount and hardware.
Power-cut testing is mandatory before real deployment. Temp files may remain
after crashes and need separately reviewed cleanup. Provisioning an initial
trusted record, authenticated peer/epoch binding, hardware anti-rollback,
cross-process enforcement against noncooperating writers and exactly-once
external effects are not provided. Do not promote this example to an
embedded/IoT production security claim solely from passing host CTests.

### POSIX syscall fault-injection hardening (experimental)

`iot_posix_syscall_fault_matrix` compiles the **actual adapter source**
under test-only syscall interposition and injects `EINTR`, `EIO`,
`ENOSPC`, short/zero writes, `read` failures, failed `openat`, `close`,
file/directory `fsync`, and `renameat` failures both before and after
the operation. The test asserts that failed pre-rename attempts leave
no visible checkpoint and cleans up their temp files; after-rename
failures may leave a visible checkpoint and MUST be handled as
`IN_DOUBT` by the ordered storage layer.

The regression also found and fixes a stale staged-slot issue: a failed
read after a successful read could previously leave `loaded[slot]` true.
Now a valid read attempt invalidates and erases the previous staged
slot, reads into a temporary buffer, then publishes new cache/output
bytes only after a complete successful read and close. Failed reads
leave caller output unchanged and prohibit flushing stale state.

The syscall harness is **not** a filesystem power-cut simulator:
test-reported errors after successful system calls are synthetic.
Do not infer POSIX/storage crash durability or end-to-end Oreslang
guest parity without independent target evidence.

### Crash-orphan cleanup and stricter checkpoint file trust

On opening the Linux host adapter, after acquiring its exclusive advisory
directory lock, the adapter scans for old temporary files matching only its
own `.slot[01].<pid>.<serial>.tmp` naming convention. It removes at most 128
matching files per open and fsyncs the directory after cleanup. It will only
delete owner-owned regular files with one hard link, exact mode `0600`, and
length at most 64 bytes. Suspicious matching symlinks, hardlinks, permissive
files or oversized files cause open to fail closed. Unrelated directory
entries are left untouched. The directory must remain exclusively dedicated
to this store and inaccessible to noncooperating writers.

Checkpoint slot reads also require owner ownership, exact private `0600`
permissions, a single hard link, regular file type and exact 64-byte size.
Failures invalidate and scrub staging and leave caller output untouched.

`iot_posix_owned_orphan_cleanup` covers owned temp removal, untouched
unrelated files, suspicious symlinks, permissions and oversized names/files.
`iot_posix_fsync_rename_process_crashes` additionally checks reopen cleanup
and fails closed for nonprivate checkpoint slots.

**Boundaries:** this is Linux host-only behavior and does not establish
anti-rollback, external protection against writers that ignore advisory
locks, or true power-loss durability. Cleaning known temporary files cannot
repair a corrupt published checkpoint, nor does it allow recovery from an
untrusted checkpoint by selecting an older generation.
