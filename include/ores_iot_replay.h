#ifndef ORES_IOT_REPLAY_H
#define ORES_IOT_REPLAY_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* NON-CRYPTOGRAPHIC reference: call only after authenticating the peer and
 * binding identity, key epoch and sequence to the verified message MAC/AEAD.
 * Caller serializes access and MUST persist an accepted watermark + bitmap
 * before any externally visible side effect. Reinitializing on reboot allows
 * replay until a trusted checkpoint is restored. */
#define ORES_IOT_REPLAY_WINDOW_BITS 64u

typedef struct {
    uint64_t highest;
    uint64_t bitmap;  /* bit N represents sequence (highest - N) */
} ores_iot_replay_window;

typedef enum {
    ORES_IOT_REPLAY_ACCEPT = 0,
    ORES_IOT_REPLAY_INVALID_ARGUMENT = 1,
    ORES_IOT_REPLAY_INVALID_SEQUENCE = 2,
    ORES_IOT_REPLAY_DUPLICATE = 3,
    ORES_IOT_REPLAY_STALE = 4
} ores_iot_replay_status;

/* Reset volatile state; NEVER use as a replay bypass after restart. */
ores_iot_replay_status ores_iot_replay_init(ores_iot_replay_window *window);
/* Restore a trusted durable checkpoint. Either both fields are zero (empty)
 * or highest>0, bit 0 is set and no set bit refers to a nonexistent seq <=0.
 * Failures leave the original window untouched. */
ores_iot_replay_status ores_iot_replay_restore(
    ores_iot_replay_window *window, uint64_t highest, uint64_t bitmap);
/* Prepare a candidate without changing a validated committed window.
 * On ACCEPT, *next receives the prospective state; on any other result it is
 * untouched. next must be a distinct object, not the committed window.
 * Under the caller's serialization boundary: authenticate the frame, preview,
 * durably checkpoint the complete candidate, then publish/commit and apply
 * externally visible effects. This API does not perform I/O or guarantee
 * atomicity across the durable store and those external effects. */
ores_iot_replay_status ores_iot_replay_preview(
    const ores_iot_replay_window *committed, uint64_t sequence,
    ores_iot_replay_window *next);

/* Mutates only on ACCEPT. No wraparound; zero sequence is permanently invalid.
 * Allows unseen authenticated messages up to 63 below the highest sequence.
 * WARNING: this primitive does not validate the authenticity of messages. */
ores_iot_replay_status ores_iot_replay_admit(
    ores_iot_replay_window *window, uint64_t sequence);

#ifdef __cplusplus
}
#endif
#endif
