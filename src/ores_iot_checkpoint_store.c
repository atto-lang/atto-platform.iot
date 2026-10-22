#include "ores_iot_checkpoint_store.h"
#include <string.h>

static int valid_io(const ores_iot_checkpoint_io *io) {
    return io && io->read_slot && io->write_bytes && io->flush &&
           io->publish_marker;
}

/* Do not update the caller's state on corrupt or ambiguous storage reads. */
static ores_iot_store_status load_slots(const ores_iot_checkpoint_io *io,
    uint64_t peer, uint64_t epoch, uint8_t a[ORES_IOT_CHECKPOINT_BYTES],
    uint8_t b[ORES_IOT_CHECKPOINT_BYTES], ores_iot_checkpoint_value *current,
    unsigned *target) {
    ores_iot_checkpoint_value first, second;
    ores_iot_checkpoint_status sa, sb, recovered;
    if (!io->read_slot(io->context,0,a) || !io->read_slot(io->context,1,b))
        return ORES_IOT_STORE_IO_ERROR;
    sa=ores_iot_checkpoint_decode(a,ORES_IOT_CHECKPOINT_BYTES,peer,epoch,&first);
    sb=ores_iot_checkpoint_decode(b,ORES_IOT_CHECKPOINT_BYTES,peer,epoch,&second);
    recovered=ores_iot_checkpoint_recover(a,b,ORES_IOT_CHECKPOINT_BYTES,
                                          peer,epoch,current);
    if (recovered==ORES_IOT_CHECKPOINT_EMPTY) return ORES_IOT_STORE_EMPTY;
    if (recovered!=ORES_IOT_CHECKPOINT_OK) return ORES_IOT_STORE_UNTRUSTED;
    if (target) {
        /* The older slot is the only safe write target. */
        if (sa==ORES_IOT_CHECKPOINT_EMPTY) *target=0u;
        else if (sb==ORES_IOT_CHECKPOINT_EMPTY) *target=1u;
        else if (sa==ORES_IOT_CHECKPOINT_OK && sb==ORES_IOT_CHECKPOINT_OK)
            *target=(first.generation<second.generation ? 0u : 1u);
        else return ORES_IOT_STORE_UNTRUSTED;
    }
    return ORES_IOT_STORE_OK;
}

ores_iot_store_status ores_iot_checkpoint_load(
    const ores_iot_checkpoint_io *io, uint64_t peer_binding,
    uint64_t key_epoch, ores_iot_checkpoint_value *out) {
    uint8_t a[ORES_IOT_CHECKPOINT_BYTES], b[ORES_IOT_CHECKPOINT_BYTES];
    ores_iot_checkpoint_value current;
    ores_iot_store_status status;
    if (!valid_io(io) || !out || !peer_binding || !key_epoch)
        return ORES_IOT_STORE_INVALID_ARGUMENT;
    status=load_slots(io,peer_binding,key_epoch,a,b,&current,0);
    if (status==ORES_IOT_STORE_OK) *out=current;
    return status;
}

ores_iot_store_status ores_iot_checkpoint_commit_sequence(
    const ores_iot_checkpoint_io *io, uint64_t peer_binding,
    uint64_t key_epoch, uint64_t expected_generation, uint64_t sequence,
    ores_iot_checkpoint_value *out) {
    uint8_t a[ORES_IOT_CHECKPOINT_BYTES], b[ORES_IOT_CHECKPOINT_BYTES];
    uint8_t candidate_bytes[ORES_IOT_CHECKPOINT_BYTES], marker[8];
    static const uint8_t unpublished[8]={0};
    ores_iot_checkpoint_value current, committed;
    ores_iot_replay_status replay;
    ores_iot_store_status status;
    unsigned target=0;
    if (!valid_io(io) || !out || !peer_binding || !key_epoch ||
        !expected_generation) return ORES_IOT_STORE_INVALID_ARGUMENT;
    status=load_slots(io,peer_binding,key_epoch,a,b,&current,&target);
    if (status!=ORES_IOT_STORE_OK) return status;
    if (current.generation!=expected_generation) return ORES_IOT_STORE_CONFLICT;
    if (current.generation==UINT64_MAX) return ORES_IOT_STORE_GENERATION_EXHAUSTED;
    replay=ores_iot_replay_preview(&current.window,sequence,&committed.window);
    if (replay==ORES_IOT_REPLAY_DUPLICATE) return ORES_IOT_STORE_REPLAY_DUPLICATE;
    if (replay==ORES_IOT_REPLAY_STALE) return ORES_IOT_STORE_REPLAY_STALE;
    if (replay==ORES_IOT_REPLAY_INVALID_SEQUENCE) return ORES_IOT_STORE_REPLAY_INVALID_SEQUENCE;
    if (replay!=ORES_IOT_REPLAY_ACCEPT) return ORES_IOT_STORE_UNTRUSTED;
    committed.generation=current.generation+1u;
    if (ores_iot_checkpoint_prepare(&committed.window,peer_binding,key_epoch,
                committed.generation,candidate_bytes)!=ORES_IOT_CHECKPOINT_OK ||
        ores_iot_checkpoint_marker(marker)!=ORES_IOT_CHECKPOINT_OK)
        return ORES_IOT_STORE_UNTRUSTED;
    /* Once any mutation starts, failures may have changed durable storage.
     * Do not allow caller to retry using stale cached state without reload. */
    if (!io->write_bytes(io->context,target,ORES_IOT_CHECKPOINT_MARKER_OFFSET,
                         unpublished,sizeof unpublished)) return ORES_IOT_STORE_IN_DOUBT;
    if (!io->flush(io->context,target)) return ORES_IOT_STORE_IN_DOUBT;
    if (!io->write_bytes(io->context,target,0u,candidate_bytes,
                         ORES_IOT_CHECKPOINT_MARKER_OFFSET)) return ORES_IOT_STORE_IN_DOUBT;
    if (!io->flush(io->context,target)) return ORES_IOT_STORE_IN_DOUBT;
    if (!io->publish_marker(io->context,target,marker)) return ORES_IOT_STORE_IN_DOUBT;
    *out=committed;
    return ORES_IOT_STORE_OK;
}
