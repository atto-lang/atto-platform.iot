#ifndef ORES_IOT_ACTOR_INGRESS_H
#define ORES_IOT_ACTOR_INGRESS_H
#include <stddef.h>
#include <stdint.h>
#include "ores_iot_checkpoint_store.h"
#include "ores_iot_frame.h"
#ifdef __cplusplus
extern "C" {
#endif

#define ORES_IOT_INGRESS_QUEUE_CAPACITY 4u

typedef enum {
    ORES_IOT_INGRESS_OK = 0,
    ORES_IOT_INGRESS_INVALID_ARGUMENT,
    ORES_IOT_INGRESS_UNAUTHENTICATED,
    ORES_IOT_INGRESS_BACKPRESSURE,
    ORES_IOT_INGRESS_EMPTY,
    ORES_IOT_INGRESS_FRAME_REJECTED,
    ORES_IOT_INGRESS_STORE_EMPTY,
    ORES_IOT_INGRESS_STORE_IO_ERROR,
    ORES_IOT_INGRESS_STORE_IN_DOUBT,
    ORES_IOT_INGRESS_STORE_UNTRUSTED,
    ORES_IOT_INGRESS_STORE_CONFLICT,
    ORES_IOT_INGRESS_GENERATION_EXHAUSTED,
    ORES_IOT_INGRESS_REPLAY_DUPLICATE,
    ORES_IOT_INGRESS_REPLAY_STALE,
    ORES_IOT_INGRESS_REPLAY_INVALID_SEQUENCE
} ores_iot_ingress_status;

typedef struct {
    uint64_t peer_binding;
    uint64_t key_epoch;
    uint64_t sequence;
    ores_iot_kind kind;
    size_t length;
    uint8_t payload[ORES_IOT_MAX_PAYLOAD_BYTES];
} ores_iot_ingress_message;

typedef struct {
    ores_iot_ingress_message queue[ORES_IOT_INGRESS_QUEUE_CAPACITY];
    uint8_t head;
    uint8_t tail;
    uint8_t count;
    uint8_t busy;
} ores_iot_actor_ingress;

/* The caller must set authenticated=1 only after cryptographically
 * authenticating the complete frame plus peer binding, key epoch and sequence.
 * The ingress itself performs no cryptography.
 *
 * Admission is serialized and fail-closed:
 *  1. reject unauthenticated input;
 *  2. reject when the bounded actor queue is already full;
 *  3. decode the exact frame into a private unpublished queue slot;
 *  4. load + durably commit replay state;
 *  5. publish the in-memory queue slot.
 *
 * No actor-visible message is published before durable replay admission.
 * frame/frame_len must not overlap the ingress object or any of its queue
 * storage. Such aliasing is rejected before decode or checkpoint I/O.
 * A crash after checkpoint publication but before actor effects can still lose
 * an effect; exactly-once semantics require an idempotent handler/outbox.
 */
ores_iot_ingress_status ores_iot_actor_ingress_init(
    ores_iot_actor_ingress *ingress);
ores_iot_ingress_status ores_iot_actor_ingress_admit(
    ores_iot_actor_ingress *ingress,
    const ores_iot_checkpoint_io *checkpoint_io,
    uint64_t peer_binding, uint64_t key_epoch, uint64_t sequence,
    int authenticated, const uint8_t *frame, size_t frame_len);

/* Sink contract: return 1 only after synchronously copying the message into
 * actor-owned storage. Return 0 for actor-mailbox backpressure. The pointer is
 * borrowed only for the callback and must never be retained. The callback must
 * not re-enter this ingress object. On 0 the message remains queued unchanged. */
typedef int (*ores_iot_actor_copy_sink)(
    void *context, const ores_iot_ingress_message *message);

ores_iot_ingress_status ores_iot_actor_ingress_dispatch_one(
    ores_iot_actor_ingress *ingress,
    ores_iot_actor_copy_sink sink, void *context);

size_t ores_iot_actor_ingress_count(const ores_iot_actor_ingress *ingress);

#ifdef __cplusplus
}
#endif
#endif
