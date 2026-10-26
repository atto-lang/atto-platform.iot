#include "ores_iot_actor_ingress.h"
#include <string.h>

static void scrub_bytes(void *address, size_t length) {
    volatile unsigned char *dst=(volatile unsigned char*)address;
    size_t i;
    for(i=0; i<length; ++i) dst[i]=0;
}

static int byte_ranges_overlap(const void *a, size_t alen,
                               const void *b, size_t blen) {
    const uintptr_t aa=(uintptr_t)a;
    const uintptr_t bb=(uintptr_t)b;
    if(alen==0 || blen==0) return 0;
    /* Fail closed if computing either end address would wrap. */
    if(alen>(size_t)(UINTPTR_MAX-aa) || blen>(size_t)(UINTPTR_MAX-bb))
        return 1;
    return aa < bb+blen && bb < aa+alen;
}

static int valid_ingress_state(const ores_iot_actor_ingress *ingress) {
    unsigned expected_tail;
    if(!ingress ||
       ingress->head>=ORES_IOT_INGRESS_QUEUE_CAPACITY ||
       ingress->tail>=ORES_IOT_INGRESS_QUEUE_CAPACITY ||
       ingress->count>ORES_IOT_INGRESS_QUEUE_CAPACITY ||
       ingress->busy>1u)
        return 0;
    expected_tail=((unsigned)ingress->head+(unsigned)ingress->count)%
                  ORES_IOT_INGRESS_QUEUE_CAPACITY;
    return ingress->tail==(uint8_t)expected_tail;
}

static ores_iot_ingress_status map_store(ores_iot_store_status status) {
    switch(status) {
        case ORES_IOT_STORE_OK: return ORES_IOT_INGRESS_OK;
        case ORES_IOT_STORE_EMPTY: return ORES_IOT_INGRESS_STORE_EMPTY;
        case ORES_IOT_STORE_IO_ERROR: return ORES_IOT_INGRESS_STORE_IO_ERROR;
        case ORES_IOT_STORE_IN_DOUBT: return ORES_IOT_INGRESS_STORE_IN_DOUBT;
        case ORES_IOT_STORE_UNTRUSTED: return ORES_IOT_INGRESS_STORE_UNTRUSTED;
        case ORES_IOT_STORE_CONFLICT: return ORES_IOT_INGRESS_STORE_CONFLICT;
        case ORES_IOT_STORE_GENERATION_EXHAUSTED:
            return ORES_IOT_INGRESS_GENERATION_EXHAUSTED;
        case ORES_IOT_STORE_REPLAY_DUPLICATE:
            return ORES_IOT_INGRESS_REPLAY_DUPLICATE;
        case ORES_IOT_STORE_REPLAY_STALE:
            return ORES_IOT_INGRESS_REPLAY_STALE;
        case ORES_IOT_STORE_REPLAY_INVALID_SEQUENCE:
            return ORES_IOT_INGRESS_REPLAY_INVALID_SEQUENCE;
        case ORES_IOT_STORE_INVALID_ARGUMENT:
        default:
            return ORES_IOT_INGRESS_INVALID_ARGUMENT;
    }
}

ores_iot_ingress_status ores_iot_actor_ingress_init(
        ores_iot_actor_ingress *ingress) {
    if(!ingress) return ORES_IOT_INGRESS_INVALID_ARGUMENT;
    scrub_bytes(ingress,sizeof *ingress);
    return ORES_IOT_INGRESS_OK;
}

size_t ores_iot_actor_ingress_count(const ores_iot_actor_ingress *ingress) {
    if(!valid_ingress_state(ingress) || ingress->busy) return 0;
    return ingress->count;
}

ores_iot_ingress_status ores_iot_actor_ingress_admit(
        ores_iot_actor_ingress *ingress,
        const ores_iot_checkpoint_io *checkpoint_io,
        uint64_t peer_binding, uint64_t key_epoch, uint64_t sequence,
        int authenticated, const uint8_t *frame, size_t frame_len) {
    ores_iot_view view;
    ores_iot_checkpoint_value committed;
    ores_iot_store_status store_status;
    ores_iot_ingress_message *slot;
    ores_iot_ingress_status result;

    if(!ingress || !checkpoint_io || !frame || frame_len==0 ||
       peer_binding==0 || key_epoch==0)
        return ORES_IOT_INGRESS_INVALID_ARGUMENT;
    if(authenticated!=1) return ORES_IOT_INGRESS_UNAUTHENTICATED;
    if(byte_ranges_overlap(frame,frame_len,ingress,sizeof *ingress))
        return ORES_IOT_INGRESS_INVALID_ARGUMENT;
    if(!valid_ingress_state(ingress) || ingress->busy)
        return ORES_IOT_INGRESS_INVALID_ARGUMENT;
    if(ingress->count >= ORES_IOT_INGRESS_QUEUE_CAPACITY)
        return ORES_IOT_INGRESS_BACKPRESSURE;

    ingress->busy=1;
    if(ores_iot_decode(frame,frame_len,&view)!=ORES_IOT_OK) {
        ingress->busy=0;
        return ORES_IOT_INGRESS_FRAME_REJECTED;
    }

    slot=&ingress->queue[ingress->tail];
    scrub_bytes(slot,sizeof *slot);
    slot->peer_binding=peer_binding;
    slot->key_epoch=key_epoch;
    slot->sequence=sequence;
    slot->kind=view.kind;
    slot->length=view.length;
    if(view.length) memcpy(slot->payload,view.payload,view.length);

    store_status=ores_iot_checkpoint_load(
        checkpoint_io,peer_binding,key_epoch,&committed);
    if(store_status!=ORES_IOT_STORE_OK) {
        result=map_store(store_status);
        scrub_bytes(slot,sizeof *slot);
        ingress->busy=0;
        return result;
    }

    store_status=ores_iot_checkpoint_commit_sequence(
        checkpoint_io,peer_binding,key_epoch,committed.generation,
        sequence,&committed);
    if(store_status!=ORES_IOT_STORE_OK) {
        result=map_store(store_status);
        scrub_bytes(slot,sizeof *slot);
        ingress->busy=0;
        return result;
    }

    ingress->tail=(uint8_t)((ingress->tail+1u)%ORES_IOT_INGRESS_QUEUE_CAPACITY);
    ingress->count++;
    ingress->busy=0;
    return ORES_IOT_INGRESS_OK;
}

ores_iot_ingress_status ores_iot_actor_ingress_dispatch_one(
        ores_iot_actor_ingress *ingress,
        ores_iot_actor_copy_sink sink, void *context) {
    ores_iot_ingress_message *slot;
    ores_iot_ingress_message snapshot;
    int accepted;
    if(!ingress || !sink || !valid_ingress_state(ingress) || ingress->busy)
        return ORES_IOT_INGRESS_INVALID_ARGUMENT;
    if(ingress->count==0) return ORES_IOT_INGRESS_EMPTY;

    ingress->busy=1;
    slot=&ingress->queue[ingress->head];

    /* Never lend a sink the live queue slot. A callback that casts away const
     * must not be able to corrupt a message that remains queued on
     * backpressure or an invalid return value. */
    snapshot=*slot;
    accepted=sink(context,&snapshot);
    scrub_bytes(&snapshot,sizeof snapshot);
    if(accepted!=0 && accepted!=1) {
        ingress->busy=0;
        return ORES_IOT_INGRESS_INVALID_ARGUMENT;
    }
    if(!accepted) {
        ingress->busy=0;
        return ORES_IOT_INGRESS_BACKPRESSURE;
    }

    scrub_bytes(slot,sizeof *slot);
    ingress->head=(uint8_t)((ingress->head+1u)%ORES_IOT_INGRESS_QUEUE_CAPACITY);
    ingress->count--;
    ingress->busy=0;
    return ORES_IOT_INGRESS_OK;
}
