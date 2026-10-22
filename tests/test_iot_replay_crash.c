/* Crash-point model for a CALLER-OWNED durable checkpoint protocol.
 * No actual disk/flash writes, fsync, atomic sectors, MACs, or power cut are
 * provided here. Demonstrates required ordering and at-most-once limitations. */
#include "ores_iot_replay.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define REQUIRE(x) do { if (!(x)) { fprintf(stderr, "check failed at %s:%d: %s\n", __FILE__, __LINE__, #x); return 1; } } while (0)

typedef struct {
    uint64_t generation;
    uint64_t highest;
    uint64_t bitmap;
    uint64_t checksum;
    unsigned published;
} checkpoint;
typedef struct { checkpoint slot[2]; } simulated_disk;

static uint64_t checksum(const checkpoint *p) {
    return p->generation ^ p->highest ^ p->bitmap ^ UINT64_C(0x9e3779b97f4a7c15);
}
static int valid(const checkpoint *p) {
    ores_iot_replay_window test;
    return p->published && p->generation != 0 && p->checksum == checksum(p) &&
           ores_iot_replay_restore(&test, p->highest, p->bitmap) == ORES_IOT_REPLAY_ACCEPT;
}
static void init_disk(simulated_disk *disk) {
    memset(disk, 0, sizeof(*disk));
    disk->slot[0].generation=1;
    disk->slot[0].highest=10;
    disk->slot[0].bitmap=1;
    disk->slot[0].checksum=checksum(&disk->slot[0]);
    disk->slot[0].published=1;
}
/* This modeled recovery FAILS CLOSED if any published slot is corrupted.
 * It never silently falls back to an older checkpoint following corruption. */
static int recover(const simulated_disk *disk, ores_iot_replay_window *out) {
    const checkpoint *chosen=NULL;
    unsigned i;
    for (i=0; i<2; ++i) {
        const checkpoint *p=&disk->slot[i];
        if (p->published && !valid(p)) return 0;
        if (valid(p) && (!chosen || p->generation>chosen->generation)) chosen=p;
    }
    if (!chosen) return 0;
    return ores_iot_replay_restore(out,chosen->highest,chosen->bitmap)==ORES_IOT_REPLAY_ACCEPT;
}
/* cut=1..5 simulates a failure after the corresponding instruction, before
 * publishing the new slot. cut=0 means no cut; successful commit is modeled
 * as an atomic durable publication. This assumption MUST be verified for a
 * real persistence implementation and is NOT guaranteed by this primitive. */
static int checkpoint_candidate(simulated_disk *disk, const ores_iot_replay_window *next,
                                unsigned cut) {
    checkpoint *p=&disk->slot[1];
    p->published=0;
    if (cut==1) return 0;
    p->generation=disk->slot[0].generation+1u;
    if (cut==2) return 0;
    p->highest=next->highest;
    if (cut==3) return 0;
    p->bitmap=next->bitmap;
    if (cut==4) return 0;
    p->checksum=checksum(p);
    if (cut==5) return 0;
    p->published=1;
    return 1;
}
static int exercise_cut(unsigned cut) {
    simulated_disk disk;
    ores_iot_replay_window committed, candidate, recovered;
    unsigned effects=0;
    init_disk(&disk);
    REQUIRE(recover(&disk,&committed));
    REQUIRE(ores_iot_replay_preview(&committed,11,&candidate)==ORES_IOT_REPLAY_ACCEPT);
    REQUIRE(committed.highest==10 && committed.bitmap==1);
    if (cut==6) { /* crash before invoking the mock persistence call */
        REQUIRE(recover(&disk,&recovered));
        REQUIRE(ores_iot_replay_preview(&recovered,11,&candidate)==ORES_IOT_REPLAY_ACCEPT);
        REQUIRE(effects==0);
        return 0;
    }
    REQUIRE(checkpoint_candidate(&disk,&candidate,cut)==(cut==0));
    REQUIRE(recover(&disk,&recovered));
    if (cut) {
        REQUIRE(recovered.highest==10 && recovered.bitmap==1);
        REQUIRE(ores_iot_replay_preview(&recovered,11,&candidate)==ORES_IOT_REPLAY_ACCEPT);
        REQUIRE(effects==0);
    } else {
        REQUIRE(recovered.highest==11 && recovered.bitmap==3);
        REQUIRE(ores_iot_replay_preview(&recovered,11,&candidate)==ORES_IOT_REPLAY_DUPLICATE);
        /* A crash after durable publication but before effect loses the
         * effect. Replay rejection alone cannot guarantee exactly-once. */
        REQUIRE(effects==0);
        committed=recovered;
        ++effects;
        REQUIRE(ores_iot_replay_preview(&committed,11,&candidate)==ORES_IOT_REPLAY_DUPLICATE);
        REQUIRE(effects==1);
    }
    return 0;
}
static int exercise_corruption(void) {
    simulated_disk disk;
    ores_iot_replay_window current,next;
    init_disk(&disk);
    REQUIRE(recover(&disk,&current));
    REQUIRE(ores_iot_replay_preview(&current,11,&next)==ORES_IOT_REPLAY_ACCEPT);
    REQUIRE(checkpoint_candidate(&disk,&next,0));
    disk.slot[1].bitmap^=UINT64_C(4);
    /* Do NOT downgrade to the old checkpoint: that could re-accept 11. */
    REQUIRE(!recover(&disk,&current));
    init_disk(&disk);
    disk.slot[0].checksum=0;
    REQUIRE(!recover(&disk,&current));
    return 0;
}
int main(void) {
    unsigned cut;
    for(cut=0;cut<=6;++cut) REQUIRE(exercise_cut(cut)==0);
    REQUIRE(exercise_corruption()==0);
    puts("iot replay preview crash-point contract: PASS");
    return 0;
}
