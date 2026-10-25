#include "ores_iot_actor_ingress.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <string.h>

#define PEER UINT64_C(0x1234567890abcdef)
#define EPOCH UINT64_C(7)
#define N ORES_IOT_CHECKPOINT_BYTES

typedef struct {
    uint8_t durable[2][N];
    uint8_t buffer[2][N];
    unsigned reads;
    unsigned writes;
    unsigned flushes;
    unsigned publishes;
    int fail_read;
    int fail_publish;
} disk;

static int rd(void *ctx,unsigned slot,uint8_t out[N]) {
    disk *d=(disk*)ctx;
    d->reads++;
    if(slot>1u || d->fail_read) return 0;
    memcpy(out,d->durable[slot],N);
    return 1;
}
static int wr(void *ctx,unsigned slot,size_t offset,
              const uint8_t *bytes,size_t count) {
    disk *d=(disk*)ctx;
    d->writes++;
    if(slot>1u || offset>N || count>N-offset) return 0;
    memcpy(d->buffer[slot]+offset,bytes,count);
    return 1;
}
static int flush_slot(void *ctx,unsigned slot) {
    disk *d=(disk*)ctx;
    d->flushes++;
    if(slot>1u) return 0;
    memcpy(d->durable[slot],d->buffer[slot],N);
    return 1;
}
static int publish(void *ctx,unsigned slot,const uint8_t marker[8]) {
    disk *d=(disk*)ctx;
    d->publishes++;
    if(slot>1u || d->fail_publish) return 0;
    memcpy(d->buffer[slot]+ORES_IOT_CHECKPOINT_MARKER_OFFSET,
           marker,ORES_IOT_CHECKPOINT_MARKER_BYTES);
    memcpy(d->durable[slot]+ORES_IOT_CHECKPOINT_MARKER_OFFSET,
           marker,ORES_IOT_CHECKPOINT_MARKER_BYTES);
    return 1;
}
static ores_iot_checkpoint_io adapter(disk *d) {
    ores_iot_checkpoint_io io={d,rd,wr,flush_slot,publish};
    return io;
}
static void init_disk(disk *d) {
    ores_iot_replay_window w={10,1};
    uint8_t marker[ORES_IOT_CHECKPOINT_MARKER_BYTES];
    memset(d,0,sizeof *d);
    assert(ores_iot_checkpoint_prepare(&w,PEER,EPOCH,1,d->durable[0])
           ==ORES_IOT_CHECKPOINT_OK);
    assert(ores_iot_checkpoint_marker(marker)==ORES_IOT_CHECKPOINT_OK);
    memcpy(d->durable[0]+ORES_IOT_CHECKPOINT_MARKER_OFFSET,
           marker,sizeof marker);
    memcpy(d->buffer,d->durable,sizeof d->buffer);
}
static ores_iot_checkpoint_value durable_value(disk *d) {
    ores_iot_checkpoint_value value={0};
    ores_iot_checkpoint_io io=adapter(d);
    assert(ores_iot_checkpoint_load(&io,PEER,EPOCH,&value)==ORES_IOT_STORE_OK);
    return value;
}
static size_t make_frame(uint64_t value,uint8_t out[ORES_IOT_MAX_FRAME_BYTES]) {
    uint8_t payload[8];
    size_t i,written=0;
    for(i=0;i<8;++i) payload[i]=(uint8_t)(value>>(i*8u));
    assert(ores_iot_encode(ORES_IOT_COMMAND,payload,sizeof payload,
                          out,ORES_IOT_MAX_FRAME_BYTES,&written)==ORES_IOT_OK);
    return written;
}
static int bytes_zero(const void *ptr,size_t len) {
    const unsigned char *p=(const unsigned char*)ptr;
    size_t i;
    for(i=0;i<len;++i) if(p[i]) return 0;
    return 1;
}

typedef struct {
    int accept;
    unsigned calls;
    ores_iot_ingress_message copied;
} actor_sink;

static int actor_copy(void *context,const ores_iot_ingress_message *message) {
    actor_sink *sink=(actor_sink*)context;
    sink->calls++;
    if(!sink->accept) return 0;
    sink->copied=*message;
    return 1;
}

static void test_auth_frame_and_duplicate_gates(void) {
    disk d;
    ores_iot_checkpoint_io io;
    ores_iot_actor_ingress ingress;
    uint8_t frame[ORES_IOT_MAX_FRAME_BYTES];
    size_t frame_len;
    unsigned before_reads;

    init_disk(&d);
    io=adapter(&d);
    assert(ores_iot_actor_ingress_init(&ingress)==ORES_IOT_INGRESS_OK);
    frame_len=make_frame(111,frame);

    before_reads=d.reads;
    assert(ores_iot_actor_ingress_admit(&ingress,&io,PEER,EPOCH,11,0,
                                        frame,frame_len)
           ==ORES_IOT_INGRESS_UNAUTHENTICATED);
    assert(d.reads==before_reads);
    assert(ores_iot_actor_ingress_count(&ingress)==0);

    before_reads=d.reads;
    assert(ores_iot_actor_ingress_admit(&ingress,&io,PEER,EPOCH,11,1,
                                        frame,1)
           ==ORES_IOT_INGRESS_FRAME_REJECTED);
    assert(d.reads==before_reads);
    assert(ores_iot_actor_ingress_count(&ingress)==0);

    assert(ores_iot_actor_ingress_admit(&ingress,&io,PEER,EPOCH,11,1,
                                        frame,frame_len)
           ==ORES_IOT_INGRESS_OK);
    assert(ores_iot_actor_ingress_count(&ingress)==1);
    assert(ores_iot_actor_ingress_admit(&ingress,&io,PEER,EPOCH,11,1,
                                        frame,frame_len)
           ==ORES_IOT_INGRESS_REPLAY_DUPLICATE);
    assert(ores_iot_actor_ingress_count(&ingress)==1);
}

static void test_queue_backpressure_precedes_checkpoint(void) {
    disk d;
    ores_iot_checkpoint_io io;
    ores_iot_actor_ingress ingress;
    uint8_t frame[ORES_IOT_MAX_FRAME_BYTES];
    size_t frame_len;
    unsigned reads,writes,flushes,publishes;
    uint64_t seq;
    ores_iot_checkpoint_value value;
    actor_sink sink;

    init_disk(&d);
    io=adapter(&d);
    assert(ores_iot_actor_ingress_init(&ingress)==ORES_IOT_INGRESS_OK);
    frame_len=make_frame(222,frame);
    for(seq=11;seq<15;++seq)
        assert(ores_iot_actor_ingress_admit(&ingress,&io,PEER,EPOCH,seq,1,
                                            frame,frame_len)
               ==ORES_IOT_INGRESS_OK);
    assert(ores_iot_actor_ingress_count(&ingress)==ORES_IOT_INGRESS_QUEUE_CAPACITY);
    value=durable_value(&d);
    assert(value.generation==5 && value.window.highest==14);

    reads=d.reads;writes=d.writes;flushes=d.flushes;publishes=d.publishes;
    assert(ores_iot_actor_ingress_admit(&ingress,&io,PEER,EPOCH,15,1,
                                        frame,frame_len)
           ==ORES_IOT_INGRESS_BACKPRESSURE);
    assert(d.reads==reads && d.writes==writes &&
           d.flushes==flushes && d.publishes==publishes);

    memset(&sink,0,sizeof sink);
    sink.accept=0;
    assert(ores_iot_actor_ingress_dispatch_one(&ingress,actor_copy,&sink)
           ==ORES_IOT_INGRESS_BACKPRESSURE);
    assert(sink.calls==1);
    assert(ores_iot_actor_ingress_count(&ingress)==ORES_IOT_INGRESS_QUEUE_CAPACITY);

    sink.accept=1;
    assert(ores_iot_actor_ingress_dispatch_one(&ingress,actor_copy,&sink)
           ==ORES_IOT_INGRESS_OK);
    assert(ores_iot_actor_ingress_count(&ingress)==ORES_IOT_INGRESS_QUEUE_CAPACITY-1u);
    assert(sink.copied.sequence==11 && sink.copied.peer_binding==PEER &&
           sink.copied.key_epoch==EPOCH && sink.copied.kind==ORES_IOT_COMMAND &&
           sink.copied.length==8);

    assert(ores_iot_actor_ingress_admit(&ingress,&io,PEER,EPOCH,15,1,
                                        frame,frame_len)
           ==ORES_IOT_INGRESS_OK);
    value=durable_value(&d);
    assert(value.generation==6 && value.window.highest==15);
}

static void test_checkpoint_failure_never_publishes_actor_message(void) {
    disk d;
    ores_iot_checkpoint_io io;
    ores_iot_actor_ingress ingress;
    uint8_t frame[ORES_IOT_MAX_FRAME_BYTES];
    size_t frame_len;

    init_disk(&d);
    d.fail_publish=1;
    io=adapter(&d);
    assert(ores_iot_actor_ingress_init(&ingress)==ORES_IOT_INGRESS_OK);
    frame_len=make_frame(333,frame);

    assert(ores_iot_actor_ingress_admit(&ingress,&io,PEER,EPOCH,11,1,
                                        frame,frame_len)
           ==ORES_IOT_INGRESS_STORE_IN_DOUBT);
    assert(ores_iot_actor_ingress_count(&ingress)==0);
    assert(bytes_zero(&ingress.queue[0],sizeof ingress.queue[0]));
}

static void test_frame_alias_rejected_before_checkpoint(void) {
    disk d;
    ores_iot_checkpoint_io io;
    ores_iot_actor_ingress ingress;
    uint8_t external[ORES_IOT_MAX_FRAME_BYTES];
    size_t frame_len;
    unsigned reads,writes,flushes,publishes;

    init_disk(&d);
    io=adapter(&d);
    assert(ores_iot_actor_ingress_init(&ingress)==ORES_IOT_INGRESS_OK);
    frame_len=make_frame(UINT64_C(0x1122334455667788),external);
    assert(frame_len<=sizeof ingress.queue[0].payload);
    memcpy(ingress.queue[0].payload,external,frame_len);
    reads=d.reads;writes=d.writes;flushes=d.flushes;publishes=d.publishes;

    assert(ores_iot_actor_ingress_admit(
               &ingress,&io,PEER,EPOCH,11,1,
               ingress.queue[0].payload,frame_len)
           ==ORES_IOT_INGRESS_INVALID_ARGUMENT);
    assert(ores_iot_actor_ingress_count(&ingress)==0);
    assert(d.reads==reads && d.writes==writes &&
           d.flushes==flushes && d.publishes==publishes);
}

static void test_owned_copy_and_scrub_after_dispatch(void) {
    disk d;
    ores_iot_checkpoint_io io;
    ores_iot_actor_ingress ingress;
    uint8_t frame[ORES_IOT_MAX_FRAME_BYTES];
    size_t frame_len;
    actor_sink sink;

    init_disk(&d);
    io=adapter(&d);
    assert(ores_iot_actor_ingress_init(&ingress)==ORES_IOT_INGRESS_OK);
    frame_len=make_frame(UINT64_C(0x8877665544332211),frame);
    assert(ores_iot_actor_ingress_admit(&ingress,&io,PEER,EPOCH,11,1,
                                        frame,frame_len)
           ==ORES_IOT_INGRESS_OK);

    memset(&sink,0,sizeof sink);
    sink.accept=1;
    assert(ores_iot_actor_ingress_dispatch_one(&ingress,actor_copy,&sink)
           ==ORES_IOT_INGRESS_OK);
    assert(sink.calls==1 && sink.copied.sequence==11 && sink.copied.length==8);
    assert(ores_iot_actor_ingress_count(&ingress)==0);
    assert(bytes_zero(&ingress.queue[0],sizeof ingress.queue[0]));
    assert(ores_iot_actor_ingress_dispatch_one(&ingress,actor_copy,&sink)
           ==ORES_IOT_INGRESS_EMPTY);
}

int main(void) {
    test_auth_frame_and_duplicate_gates();
    test_queue_backpressure_precedes_checkpoint();
    test_checkpoint_failure_never_publishes_actor_message();
    test_owned_copy_and_scrub_after_dispatch();
    test_frame_alias_rejected_before_checkpoint();
    return 0;
}
