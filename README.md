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
