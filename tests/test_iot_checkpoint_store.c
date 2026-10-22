#include "ores_iot_checkpoint_store.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define PEER UINT64_C(0x1234567890abcdef)
#define EPOCH UINT64_C(7)
#define N ORES_IOT_CHECKPOINT_BYTES

typedef struct {
    uint8_t durable[2][N];
    uint8_t buffer[2][N];
    unsigned operation, fail_step;
    size_t partial;
    int ambiguous_commit, fail_read;
    char trace[16];
    size_t used;
} disk;
static void record(disk *d,char letter) {
    if (d->used+1<sizeof d->trace) {
        d->trace[d->used++]=letter;
        d->trace[d->used]=0;
    }
}
static int rd(void *ctx,unsigned slot,uint8_t out[N]) {
    disk *d=ctx;
    record(d,'R');
    if (slot>1u || d->fail_read) return 0;
    memcpy(out,d->durable[slot],N);return 1;
}
static int wr(void *ctx,unsigned slot,size_t offset,const uint8_t *bytes,size_t count) {
    disk *d=ctx;
    record(d,'W');
    ++d->operation;
    if (slot>1u || offset>N || count>N-offset) return 0;
    if (d->operation==d->fail_step) {
        size_t partial=d->partial<count ? d->partial:count;
        memcpy(d->buffer[slot]+offset,bytes,partial);
        return 0;
    }
    memcpy(d->buffer[slot]+offset,bytes,count);
    return 1;
}
static int sync_slot(void *ctx,unsigned slot) {
    disk *d=ctx;
    record(d,'F');
    ++d->operation;
    if (slot>1u) return 0;
    memcpy(d->durable[slot],d->buffer[slot],N);
    return d->operation!=d->fail_step;
}
static int publish(void *ctx,unsigned slot,const uint8_t marker[8]) {
    disk *d=ctx;
    record(d,'P');
    ++d->operation;
    if (slot>1u) return 0;
    /* Simulate an atomic durable 8-byte commit ONLY in the mock. */
    if (d->operation==d->fail_step && !d->ambiguous_commit) return 0;
    memcpy(d->buffer[slot]+56,marker,8);
    memcpy(d->durable[slot]+56,marker,8);
    return d->operation!=d->fail_step;
}
static ores_iot_checkpoint_io adapter(disk *d) {
    ores_iot_checkpoint_io io={d,rd,wr,sync_slot,publish};
    return io;
}
static void init_disk(disk *d,uint64_t generation) {
    ores_iot_replay_window w={10,1};
    uint8_t marker[8];
    memset(d,0,sizeof *d);
    assert(ores_iot_checkpoint_prepare(&w,PEER,EPOCH,generation,d->durable[0])
           ==ORES_IOT_CHECKPOINT_OK);
    assert(ores_iot_checkpoint_marker(marker)==ORES_IOT_CHECKPOINT_OK);
    memcpy(d->durable[0]+56,marker,8);
    memcpy(d->buffer,d->durable,sizeof d->durable);
}
static ores_iot_store_status load(disk *d,ores_iot_checkpoint_value *out) {
    ores_iot_checkpoint_io io=adapter(d);
    return ores_iot_checkpoint_load(&io,PEER,EPOCH,out);
}
static ores_iot_store_status commit(disk *d,uint64_t expected,uint64_t sequence,
                                   ores_iot_checkpoint_value *out) {
    ores_iot_checkpoint_io io=adapter(d);
    return ores_iot_checkpoint_commit_sequence(&io,PEER,EPOCH,expected,sequence,out);
}
static void expect_old(disk *d) {
    ores_iot_checkpoint_value got;
    assert(load(d,&got)==ORES_IOT_STORE_OK);
    assert(got.generation==1 && got.window.highest==10 && got.window.bitmap==1);
}
static void expect_new(disk *d) {
    ores_iot_checkpoint_value got;
    assert(load(d,&got)==ORES_IOT_STORE_OK);
    assert(got.generation==2 && got.window.highest==11 && got.window.bitmap==3);
}
static void test_success(void) {
    disk d;ores_iot_checkpoint_value value={999,{99,7}},before;
    ores_iot_checkpoint_io io;
    init_disk(&d,1);
    expect_old(&d);
    memset(d.trace,0,sizeof d.trace);d.used=0;
    assert(commit(&d,1,11,&value)==ORES_IOT_STORE_OK);
    assert(!strcmp(d.trace,"RRWFWFP"));
    assert(value.generation==2 && value.window.highest==11 && value.window.bitmap==3);
    expect_new(&d);
    before=value;
    assert(commit(&d,1,12,&value)==ORES_IOT_STORE_CONFLICT);
    assert(!memcmp(&value,&before,sizeof value));
    assert(commit(&d,2,11,&value)==ORES_IOT_STORE_REPLAY_DUPLICATE);
    assert(commit(&d,2,0,&value)==ORES_IOT_STORE_REPLAY_INVALID_SEQUENCE);
    assert(!memcmp(&value,&before,sizeof value));
    assert(commit(&d,2,12,&value)==ORES_IOT_STORE_OK);
    assert(value.generation==3 && value.window.highest==12 && value.window.bitmap==7);
    assert(load(&d,&value)==ORES_IOT_STORE_OK);
    assert(value.generation==3);
    assert(d.durable[0][56]=='O'); /* Previous generation reused, not newest. */
    /* I/O initialization and fail-closed admission */
    io=adapter(&d);
    assert(ores_iot_checkpoint_load(0,PEER,EPOCH,&value)==ORES_IOT_STORE_INVALID_ARGUMENT);
    io.publish_marker=0;
    assert(ores_iot_checkpoint_commit_sequence(&io,PEER,EPOCH,3,13,&value)==ORES_IOT_STORE_INVALID_ARGUMENT);
    d.fail_read=1;
    assert(load(&d,&value)==ORES_IOT_STORE_IO_ERROR);
    d.fail_read=0;
    d.durable[1][50]^=1;
    assert(load(&d,&value)==ORES_IOT_STORE_UNTRUSTED);
}
static void test_empty_stale_overflow(void) {
    disk d;ores_iot_checkpoint_value value={4321,{333,9}};
    init_disk(&d,1);
    memset(d.durable,0,sizeof d.durable);
    assert(load(&d,&value)==ORES_IOT_STORE_EMPTY);
    assert(commit(&d,1,11,&value)==ORES_IOT_STORE_EMPTY);
    assert(value.generation==4321);
    init_disk(&d,UINT64_MAX);
    assert(commit(&d,UINT64_MAX,11,&value)==ORES_IOT_STORE_GENERATION_EXHAUSTED);
    assert(value.generation==4321);
    init_disk(&d,1);
    assert(commit(&d,1,1,&value)==ORES_IOT_STORE_OK);
    assert(commit(&d,2,1,&value)==ORES_IOT_STORE_REPLAY_DUPLICATE);
    assert(commit(&d,2,1000,&value)==ORES_IOT_STORE_OK);
    assert(commit(&d,3,10,&value)==ORES_IOT_STORE_REPLAY_STALE);
}
static void test_failpoints(void) {
    disk d;
    unsigned step;
    size_t partial;
    ores_iot_checkpoint_value value={999,{99,7}},before=value;
    for(step=1;step<=5;++step) {
        size_t bound=(step==1 ? 8u : step==3 ? 56u : 1u);
        for(partial=0;partial<=bound;++partial) {
            init_disk(&d,1);
            d.fail_step=step;d.partial=partial;
            value=before;
            assert(commit(&d,1,11,&value)==ORES_IOT_STORE_IN_DOUBT);
            assert(!memcmp(&value,&before,sizeof value));
            /* Power loss discards the volatile buffer, durable state survives. */
            memcpy(d.buffer,d.durable,sizeof d.buffer);
            d.fail_step=0;
            expect_old(&d);
        }
    }
    init_disk(&d,1);
    d.fail_step=5;d.ambiguous_commit=1;
    value=before;
    assert(commit(&d,1,11,&value)==ORES_IOT_STORE_IN_DOUBT);
    assert(!memcmp(&value,&before,sizeof value));
    /* Commit may have succeeded even when callback reports failure. */
    expect_new(&d);
    assert(commit(&d,1,11,&value)==ORES_IOT_STORE_CONFLICT);
    assert(commit(&d,2,11,&value)==ORES_IOT_STORE_REPLAY_DUPLICATE);
}
int main(void) {
    test_success();test_empty_stale_overflow();test_failpoints();
    puts("IoT ordered checkpoint adapter fault matrix: PASS");
    return 0;
}
