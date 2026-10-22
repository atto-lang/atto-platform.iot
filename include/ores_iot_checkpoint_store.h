#ifndef ORES_IOT_CHECKPOINT_STORE_H
#define ORES_IOT_CHECKPOINT_STORE_H
#include "ores_iot_checkpoint.h"
#ifdef __cplusplus
extern "C" {
#endif

/* Caller-owned storage adapter: two separate fixed 64-byte slots. The caller
 * serializes all calls for each authenticated peer/key epoch, owns storage
 * lifetime, and must make read_slot observe the latest durable state.
 * All functions return 1 on success, 0 on failure.
 *
 * write_bytes may tear or partially succeed. flush must make ALL preceding
 * successful writes to the selected slot durable in their issued order.
 * publish_marker MUST atomically publish exactly the eight-byte final marker
 * and return success ONLY after it is DURABLE (including any needed barriers).
 * The target storage/firmware must prove these properties on power loss.
 * If it cannot, do not use this interface for replay protection.
 * No callback may re-enter the checkpoint API for the same peer/epoch.
 * The callbacks must never silently downgrade a failed write to success. */
typedef struct {
    void *context;
    int (*read_slot)(void *context, unsigned slot, uint8_t out[ORES_IOT_CHECKPOINT_BYTES]);
    int (*write_bytes)(void *context, unsigned slot, size_t offset,
                       const uint8_t *bytes, size_t length);
    int (*flush)(void *context, unsigned slot);
    int (*publish_marker)(void *context, unsigned slot,
                          const uint8_t marker[ORES_IOT_CHECKPOINT_MARKER_BYTES]);
} ores_iot_checkpoint_io;

typedef enum {
    ORES_IOT_STORE_OK=0,
    ORES_IOT_STORE_INVALID_ARGUMENT,
    ORES_IOT_STORE_IO_ERROR,
    ORES_IOT_STORE_IN_DOUBT,
    ORES_IOT_STORE_UNTRUSTED,
    ORES_IOT_STORE_EMPTY,
    ORES_IOT_STORE_CONFLICT,
    ORES_IOT_STORE_GENERATION_EXHAUSTED,
    ORES_IOT_STORE_REPLAY_DUPLICATE,
    ORES_IOT_STORE_REPLAY_STALE,
    ORES_IOT_STORE_REPLAY_INVALID_SEQUENCE
} ores_iot_store_status;

/* Read and validate both slots from durable storage; no side effects.
 * On all failures *out remains unchanged. A completely empty store fails
 * CLOSED; trust-bound provisioning of the first checkpoint is out of scope. */
ores_iot_store_status ores_iot_checkpoint_load(
    const ores_iot_checkpoint_io *io, uint64_t peer_binding,
    uint64_t key_epoch, ores_iot_checkpoint_value *out);

/* Authenticate the message FIRST. Re-read and compare expected generation
 * (optimistic concurrency check), preview sequence, then perform:
 * 1. invalidate the inactive slot marker with all-zero bytes; flush;
 * 2. write bytes 0..55 of candidate; flush;
 * 3. atomically publish and durably flush final marker.
 * The prior committed slot is never written by this operation.
 * The caller must discard cached state on IN_DOUBT and re-read/verify store
 * before further processing. No output changes unless successfully committed.
 * This enforces at-most-once REPLAY ADMISSION, not exactly-once effects.
 * Does not provide cross-process locking, anti-rollback hardware, or crypto. */
ores_iot_store_status ores_iot_checkpoint_commit_sequence(
    const ores_iot_checkpoint_io *io, uint64_t peer_binding,
    uint64_t key_epoch, uint64_t expected_generation, uint64_t sequence,
    ores_iot_checkpoint_value *out);
#ifdef __cplusplus
}
#endif
#endif
