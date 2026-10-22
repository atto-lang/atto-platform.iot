#include "ores_iot_checkpoint.h"
#include <string.h>

static const uint8_t marker[ORES_IOT_CHECKPOINT_MARKER_BYTES] = {
    'O','R','E','C','O','M','0','1'
};

static void store64(uint8_t *p, uint64_t value) {
    unsigned i;
    for (i=0; i<8u; ++i) p[i] = (uint8_t)(value >> (8u*i));
}
static uint64_t load64(const uint8_t *p) {
    uint64_t value=0;
    unsigned i;
    for (i=0; i<8u; ++i) value |= (uint64_t)p[i] << (8u*i);
    return value;
}
static void store32(uint8_t *p, uint32_t value) {
    unsigned i;
    for (i=0; i<4u; ++i) p[i] = (uint8_t)(value >> (8u*i));
}
static uint32_t load32(const uint8_t *p) {
    uint32_t value=0;
    unsigned i;
    for (i=0; i<4u; ++i) value |= (uint32_t)p[i] << (8u*i);
    return value;
}
static uint32_t crc32c(const uint8_t *p, size_t length) {
    uint32_t crc=UINT32_MAX;
    size_t i;
    for (i=0; i<length; ++i) {
        unsigned b;
        crc ^= p[i];
        for(b=0; b<8u; ++b)
            crc = (crc >> 1) ^ ((crc & 1u) ? UINT32_C(0x82f63b78) : 0u);
    }
    return crc ^ UINT32_MAX;
}
/* Flat-address host reference: callers already require valid pointer ranges.
 * Span arithmetic uses subtraction (not an overflowing end address). */
static int overlaps(const void *a, size_t an, const void *b, size_t bn) {
    uintptr_t x,y;
    if (!a || !b || !an || !bn) return 0;
    x=(uintptr_t)a; y=(uintptr_t)b;
    return x>=y ? x-y<bn : y-x<an;
}

 ores_iot_checkpoint_status ores_iot_checkpoint_prepare(
    const ores_iot_replay_window *window, uint64_t peer_binding,
    uint64_t key_epoch, uint64_t generation,
    uint8_t out[ORES_IOT_CHECKPOINT_BYTES]) {
    uint8_t tmp[ORES_IOT_CHECKPOINT_BYTES]={0};
    ores_iot_replay_window checked;
    uint32_t crc;
    if (!window || !out || !peer_binding || !key_epoch || !generation ||
        overlaps(window,sizeof(*window),out,ORES_IOT_CHECKPOINT_BYTES))
        return ORES_IOT_CHECKPOINT_INVALID_ARGUMENT;
    if (ores_iot_replay_restore(&checked,window->highest,window->bitmap)
        != ORES_IOT_REPLAY_ACCEPT) return ORES_IOT_CHECKPOINT_INVALID_ARGUMENT;
    tmp[0]='O'; tmp[1]='R'; tmp[2]='C'; tmp[3]='P';
    tmp[4]=ORES_IOT_CHECKPOINT_VERSION;
    store64(tmp+8,peer_binding);
    store64(tmp+16,key_epoch);
    store64(tmp+24,generation);
    store64(tmp+32,checked.highest);
    store64(tmp+40,checked.bitmap);
    crc=crc32c(tmp,48u);
    store32(tmp+48,crc);
    store32(tmp+52,~crc);
    memcpy(out,tmp,sizeof tmp);
    return ORES_IOT_CHECKPOINT_OK;
}

ores_iot_checkpoint_status ores_iot_checkpoint_marker(
    uint8_t out[ORES_IOT_CHECKPOINT_MARKER_BYTES]) {
    if (!out) return ORES_IOT_CHECKPOINT_INVALID_ARGUMENT;
    memcpy(out,marker,sizeof marker);
    return ORES_IOT_CHECKPOINT_OK;
}

ores_iot_checkpoint_status ores_iot_checkpoint_decode(
    const uint8_t *bytes, size_t length, uint64_t peer_binding,
    uint64_t key_epoch, ores_iot_checkpoint_value *out) {
    ores_iot_checkpoint_value candidate;
    uint32_t crc;
    unsigned i;
    int empty=1;
    if (!bytes || !out || length != ORES_IOT_CHECKPOINT_BYTES ||
        !peer_binding || !key_epoch || overlaps(bytes,length,out,sizeof(*out)))
        return ORES_IOT_CHECKPOINT_INVALID_ARGUMENT;
    for(i=0;i<ORES_IOT_CHECKPOINT_MARKER_BYTES;++i)
        if(bytes[ORES_IOT_CHECKPOINT_MARKER_OFFSET+i]!=0) empty=0;
    if(empty) return ORES_IOT_CHECKPOINT_EMPTY;
    if(memcmp(bytes+ORES_IOT_CHECKPOINT_MARKER_OFFSET,marker,sizeof marker))
        return ORES_IOT_CHECKPOINT_CORRUPT;
    if(bytes[0]!='O' || bytes[1]!='R' || bytes[2]!='C' || bytes[3]!='P')
        return ORES_IOT_CHECKPOINT_CORRUPT;
    if(bytes[4]!=ORES_IOT_CHECKPOINT_VERSION)
        return ORES_IOT_CHECKPOINT_UNSUPPORTED_VERSION;
    if(bytes[5]!=0 || bytes[6]!=0 || bytes[7]!=0)
        return ORES_IOT_CHECKPOINT_CORRUPT;
    crc=crc32c(bytes,48u);
    if(load32(bytes+48)!=crc || load32(bytes+52)!=~crc)
        return ORES_IOT_CHECKPOINT_CORRUPT;
    if(load64(bytes+8)!=peer_binding || load64(bytes+16)!=key_epoch)
        return ORES_IOT_CHECKPOINT_FOREIGN_CONTEXT;
    candidate.generation=load64(bytes+24);
    if(!candidate.generation ||
       ores_iot_replay_restore(&candidate.window,load64(bytes+32),load64(bytes+40))
           != ORES_IOT_REPLAY_ACCEPT)
        return ORES_IOT_CHECKPOINT_CORRUPT;
    *out=candidate;
    return ORES_IOT_CHECKPOINT_OK;
}

ores_iot_checkpoint_status ores_iot_checkpoint_recover(
    const uint8_t *slot_a, const uint8_t *slot_b, size_t slot_length,
    uint64_t peer_binding, uint64_t key_epoch,
    ores_iot_checkpoint_value *out) {
    ores_iot_checkpoint_value a,b;
    ores_iot_checkpoint_status sa,sb;
    if(!slot_a || !slot_b || !out ||
       overlaps(slot_a,slot_length,slot_b,slot_length) ||
       overlaps(slot_a,slot_length,out,sizeof(*out)) ||
       overlaps(slot_b,slot_length,out,sizeof(*out)))
        return ORES_IOT_CHECKPOINT_INVALID_ARGUMENT;
    sa=ores_iot_checkpoint_decode(slot_a,slot_length,peer_binding,key_epoch,&a);
    sb=ores_iot_checkpoint_decode(slot_b,slot_length,peer_binding,key_epoch,&b);
    if(sa!=ORES_IOT_CHECKPOINT_OK && sa!=ORES_IOT_CHECKPOINT_EMPTY) return sa;
    if(sb!=ORES_IOT_CHECKPOINT_OK && sb!=ORES_IOT_CHECKPOINT_EMPTY) return sb;
    if(sa==ORES_IOT_CHECKPOINT_EMPTY && sb==ORES_IOT_CHECKPOINT_EMPTY)
        return ORES_IOT_CHECKPOINT_EMPTY;
    if(sa==ORES_IOT_CHECKPOINT_OK && sb==ORES_IOT_CHECKPOINT_OK) {
        if(a.generation==b.generation) return ORES_IOT_CHECKPOINT_CORRUPT;
        *out=a.generation>b.generation?a:b;
    } else *out=(sa==ORES_IOT_CHECKPOINT_OK?a:b);
    return ORES_IOT_CHECKPOINT_OK;
}
