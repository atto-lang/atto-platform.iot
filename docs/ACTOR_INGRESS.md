# Authenticated replay-gated actor ingress

This module is a host/runtime integration contract between authenticated IoT
transport input and a bounded Oreslang actor mailbox. It is intentionally not
an MQTT/CoAP/TLS implementation and performs no cryptography.

## Ordering invariant

For each admitted message:

1. The caller has already authenticated the complete frame plus peer binding,
   key epoch and sequence number.
2. If the actor ingress queue is already full, return backpressure **before**
   reading or advancing durable replay state.
3. Decode the exact frame into a private unpublished queue slot.
4. Load the durable replay checkpoint and commit the candidate sequence.
5. Only after durable replay admission succeeds, publish the in-memory queue
   slot for actor delivery.
6. The actor sink must synchronously copy the message into actor-owned storage
   before returning success. The ingress then scrubs its copy.

This prevents an unauthenticated, duplicate, stale, malformed, failed-store or
backpressured message from becoming actor-visible.

## Backpressure

The ingress has four fixed slots. It never allocates or grows the queue.
A full queue returns `ORES_IOT_INGRESS_BACKPRESSURE` without touching durable
replay state, so the same authenticated sequence can be retried after the actor
mailbox drains. The dispatch sink may also return 0 for downstream actor
backpressure; the queued item remains unchanged and ordered.

## Failure semantics

Checkpoint `IN_DOUBT`, corruption/untrusted state, generation conflicts,
duplicate/stale sequences and I/O errors leave the actor queue unpublished.
A staged slot is scrubbed before returning.

A crash after durable replay publication but before the actor effect can still
lose that effect. This adapter therefore provides at-most-once replay
**admission**, not exactly-once application effects. Use an idempotent handler,
transactional outbox, or equivalent durable effect protocol when stronger
semantics are required.

## Ownership boundary

`ores_iot_stream` and `ores_iot_decode` expose borrowed payloads. The ingress
copies the payload into fixed owned storage before checkpoint commit. Dispatch
then exposes that owned queue item only for the duration of the sink callback;
the sink must synchronously copy it into actor-owned storage and must not retain
the pointer.

The current adapter is a C/runtime contract. Connecting the sink directly to a
compiler-emitted Oreslang actor mailbox is the next platform/compiler
integration gate.
