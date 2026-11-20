#ifndef ORES_IOT_CHECKPOINT_H
#define ORES_IOT_CHECKPOINT_H
#include <stddef.h>
#include <stdint.h>
#include "ores_iot_replay.h"
#ifdef __cplusplus
extern "C" {
#endif

#define ORES_IOT_CHECKPOINT_BYTES 64u
#define ORES_IOT_CHECKPOINT_MARKER_OFFSET 56u
#define ORES_IOT_CHECKPOINT_MARKER_BYTES 8u
#define ORES_IOT_CHECKPOINT_VERSION 1u

typedef enum {
    ORES_IOT_CHECKPOINT_OK = 0,
    ORES_IOT_CHECKPOINT_EMPTY = 1,
    ORES_IOT_CHECKPOINT_INVALID_ARGUMENT = 2,
    ORES_IOT_CHECKPOINT_CORRUPT = 3,
    ORES_IOT_CHECKPOINT_FOREIGN_CONTEXT = 4,
    ORES_IOT_CHECKPOINT_UNSUPPORTED_VERSION = 5
} ores_iot_checkpoint_status;

typedef struct {
    uint64_t generation;
    ores_iot_replay_window window;
} ores_iot_checkpoint_value;

/* Portable format v1, exactly 64 bytes:
 *  0..3 magic O R C P, 4 version, 5..7 zero reserved
 *  8..15 peer identity binding, 16..23 key epoch, 24..31 generation,
 * 32..39 highest, 40..47 bitmap (all integers little-endian),
 * 48..51 CRC32C over bytes 0..47, 52..55 bitwise complement of CRC,
 * 56..63 final publication marker "ORECOM01" or all-zero unpublished.
 * CRC detects accidental corruption, NOT forgery or malicious rollback.
 * peer_binding and epoch must come from authenticated, trusted metadata.
 * This is a codec, not a durable storage implementation.
 * The caller MUST guarantee a prior committed slot remains durable; make the
 * target slot unpublished and durably flush that invalidation; persist and
 * durably flush bytes 0..55; only then write + flush the 8-byte marker as the
 * FINAL atomic durable publication operation. No side effect may precede the
 * final durability barrier. An implementation without atomic marker writes and
 * power-fail ordering guarantees MUST NOT use this protocol in production.
 * Serialize all updates per peer/epoch; use externally trusted monotonic state
 * to prevent adversarial rollback of both slots. */

/* Builds an unpublished record; writes out only on success. No allocation.
 * All buffers must be valid and non-overlapping. Generation and bindings > 0. */
ores_iot_checkpoint_status ores_iot_checkpoint_prepare(
    const ores_iot_replay_window *window, uint64_t peer_binding,
    uint64_t key_epoch, uint64_t generation,
    uint8_t out[ORES_IOT_CHECKPOINT_BYTES]);

/* Copies the FINAL publication marker; caller alone guarantees flush/order. */
ores_iot_checkpoint_status ores_iot_checkpoint_marker(
    uint8_t out[ORES_IOT_CHECKPOINT_MARKER_BYTES]);

/* Strictly requires exact record length and matching authenticated context.
 * Empty (zero marker) slots are ignored, even if their preparation bytes are
 * incomplete. All failures leave out unchanged. */
ores_iot_checkpoint_status ores_iot_checkpoint_decode(
    const uint8_t *bytes, size_t length, uint64_t peer_binding,
    uint64_t key_epoch, ores_iot_checkpoint_value *out);

/* Reconstructs highest generation from two redundant slots. Any recognized
 * published corruption, foreign context or conflicting generation FAILS CLOSED
 * (never downgrade silently). No valid slot => EMPTY. On failure out unchanged.
 * A valid older slot can be used only after the storage-specific atomic marker
 * guarantee is proven, and assumes no malicious rollback of both slots. */
ores_iot_checkpoint_status ores_iot_checkpoint_recover(
    const uint8_t *slot_a, const uint8_t *slot_b, size_t slot_length,
    uint64_t peer_binding, uint64_t key_epoch,
    ores_iot_checkpoint_value *out);
#ifdef __cplusplus
}
#endif
#endif
