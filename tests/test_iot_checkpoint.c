#include "ores_iot_checkpoint.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define PEER UINT64_C(0x1234567890abcdef)
#define EPOCH UINT64_C(7)
static void publish(uint8_t *slot) {
    assert(ores_iot_checkpoint_marker(slot+ORES_IOT_CHECKPOINT_MARKER_OFFSET)
        == ORES_IOT_CHECKPOINT_OK);
}
static void check_original(const ores_iot_checkpoint_value *value) {
    assert(value->generation==1);
    assert(value->window.highest==10);
    assert(value->window.bitmap==1);
}
static void test_basics(void) {
    uint8_t a[ORES_IOT_CHECKPOINT_BYTES],b[ORES_IOT_CHECKPOINT_BYTES];
    ores_iot_replay_window w={10,1};
    ores_iot_checkpoint_value out={123,{777,9}},saved;
    size_t i;
    assert(ores_iot_checkpoint_prepare(&w,PEER,EPOCH,1,a)==ORES_IOT_CHECKPOINT_OK);
    for(i=56;i<64;++i) assert(a[i]==0);
    saved=out;
    assert(ores_iot_checkpoint_decode(a,sizeof a,PEER,EPOCH,&out)==ORES_IOT_CHECKPOINT_EMPTY);
    assert(memcmp(&out,&saved,sizeof out)==0);
    publish(a);
    assert(ores_iot_checkpoint_decode(a,sizeof a,PEER,EPOCH,&out)==ORES_IOT_CHECKPOINT_OK);
    check_original(&out);
    assert(ores_iot_checkpoint_decode(a,sizeof a,PEER,EPOCH+1,&out)==ORES_IOT_CHECKPOINT_FOREIGN_CONTEXT);
    assert(ores_iot_checkpoint_decode(a,sizeof a,PEER+1,EPOCH,&out)==ORES_IOT_CHECKPOINT_FOREIGN_CONTEXT);
    assert(ores_iot_checkpoint_decode(a,63,PEER,EPOCH,&out)==ORES_IOT_CHECKPOINT_INVALID_ARGUMENT);
    assert(ores_iot_checkpoint_decode(a,sizeof a,PEER,EPOCH,(ores_iot_checkpoint_value *)(void *)(a+16))==ORES_IOT_CHECKPOINT_INVALID_ARGUMENT);
    { union { ores_iot_replay_window window; uint8_t bytes[64]; } same;
      same.window=w;
      assert(ores_iot_checkpoint_prepare(&same.window,PEER,EPOCH,1,same.bytes)==ORES_IOT_CHECKPOINT_INVALID_ARGUMENT);
    }
    assert(ores_iot_checkpoint_prepare(&w,0,EPOCH,1,b)==ORES_IOT_CHECKPOINT_INVALID_ARGUMENT);
    assert(ores_iot_checkpoint_prepare(&w,PEER,0,1,b)==ORES_IOT_CHECKPOINT_INVALID_ARGUMENT);
    assert(ores_iot_checkpoint_prepare(&w,PEER,EPOCH,0,b)==ORES_IOT_CHECKPOINT_INVALID_ARGUMENT);
    assert(ores_iot_checkpoint_recover(a,a,sizeof a,PEER,EPOCH,&out)==ORES_IOT_CHECKPOINT_INVALID_ARGUMENT);
    memcpy(b,a,sizeof b);
    assert(ores_iot_checkpoint_recover(a,b,sizeof a,PEER,EPOCH,&out)==ORES_IOT_CHECKPOINT_CORRUPT);
    b[4]=2;
    assert(ores_iot_checkpoint_decode(b,sizeof b,PEER,EPOCH,&out)==ORES_IOT_CHECKPOINT_UNSUPPORTED_VERSION);
    memcpy(b,a,sizeof b); b[30]^=1u;
    assert(ores_iot_checkpoint_recover(a,b,sizeof a,PEER,EPOCH,&out)==ORES_IOT_CHECKPOINT_CORRUPT);
    memcpy(b,a,sizeof b); b[56]^=1u;
    assert(ores_iot_checkpoint_decode(b,sizeof b,PEER,EPOCH,&out)==ORES_IOT_CHECKPOINT_CORRUPT);
    memcpy(b,a,sizeof b); b[52]^=1u;
    assert(ores_iot_checkpoint_decode(b,sizeof b,PEER,EPOCH,&out)==ORES_IOT_CHECKPOINT_CORRUPT);
    memcpy(b,a,sizeof b); b[6]=1;
    assert(ores_iot_checkpoint_decode(b,sizeof b,PEER,EPOCH,&out)==ORES_IOT_CHECKPOINT_CORRUPT);
    w.highest=0; w.bitmap=1;
    assert(ores_iot_checkpoint_prepare(&w,PEER,EPOCH,1,b)==ORES_IOT_CHECKPOINT_INVALID_ARGUMENT);
}
static void test_all_byte_cuts(void) {
    uint8_t old[64],prepared[64],slot[64],marker[8], empty[64]={0}, empty_b[64]={0};
    ores_iot_replay_window old_w={10,1}, proposed;
    ores_iot_checkpoint_value got={0,{0,0}};
    size_t cut,i;
    assert(ores_iot_replay_preview(&old_w,11,&proposed)==ORES_IOT_REPLAY_ACCEPT);
    assert(ores_iot_checkpoint_prepare(&old_w,PEER,EPOCH,1,old)==ORES_IOT_CHECKPOINT_OK);
    assert(ores_iot_checkpoint_prepare(&proposed,PEER,EPOCH,2,prepared)==ORES_IOT_CHECKPOINT_OK);
    publish(old);
    assert(ores_iot_checkpoint_marker(marker)==ORES_IOT_CHECKPOINT_OK);
    assert(ores_iot_checkpoint_recover(old,empty,64,PEER,EPOCH,&got)==ORES_IOT_CHECKPOINT_OK);
    check_original(&got);
    /* All 56 interrupted payload/CRC writes, with marker still absent. */
    for(cut=0;cut<=56;++cut) {
        memset(slot,0,sizeof slot);
        memcpy(slot,prepared,cut);
        assert(ores_iot_checkpoint_recover(old,slot,64,PEER,EPOCH,&got)==ORES_IOT_CHECKPOINT_OK);
        check_original(&got);
    }
    /* A partial marker must NEVER cause fallback (except all-zero marker). */
    for(cut=1;cut<8;++cut) {
        memcpy(slot,prepared,64);
        memcpy(slot+56,marker,cut);
        assert(ores_iot_checkpoint_recover(old,slot,64,PEER,EPOCH,&got)==ORES_IOT_CHECKPOINT_CORRUPT);
    }
    memcpy(slot,prepared,64);publish(slot);
    assert(ores_iot_checkpoint_recover(old,slot,64,PEER,EPOCH,&got)==ORES_IOT_CHECKPOINT_OK);
    assert(got.generation==2 && got.window.highest==11 && got.window.bitmap==3);
    /* All mutable bytes of a published slot are integrity-checked. */
    for(i=0;i<64;++i) {
        uint8_t saved=slot[i];
        slot[i]^=UINT8_C(0x80);
        assert(ores_iot_checkpoint_recover(old,slot,64,PEER,EPOCH,&got)!=ORES_IOT_CHECKPOINT_OK);
        slot[i]=saved;
    }
    /* Reject replay even when a post-durable-pre-effect crash loses effect. */
    assert(ores_iot_replay_preview(&got.window,11,&proposed)==ORES_IOT_REPLAY_DUPLICATE);
    assert(ores_iot_checkpoint_recover(empty,empty_b,64,PEER,EPOCH,&got)==ORES_IOT_CHECKPOINT_EMPTY);
}
int main(void) {
    test_basics();
    test_all_byte_cuts();
    puts("IoT portable checkpoint torn-write contract: PASS");
    return 0;
}
