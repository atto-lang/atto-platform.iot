#ifndef ORES_IOT_CHECKPOINT_POSIX_H
#define ORES_IOT_CHECKPOINT_POSIX_H
#include "ores_iot_checkpoint_store.h"

/* Linux host reference, not a substitute for verified flash/SSD power-cut
 * guarantees. The caller supplies a private (0700), owned, stable directory.
 * Acquire an exclusive, nonblocking advisory flock on the directory for the
 * entire adapter lifetime. The directory must remain private and stable.
 * Do not share an opened adapter across fork; re-open in the child. */
typedef struct {
    int dirfd;
    unsigned long serial;
    long owner_pid;
    uint8_t staged[2][ORES_IOT_CHECKPOINT_BYTES];
    unsigned char loaded[2];
} ores_iot_checkpoint_posix;

/* On success, init opens a trusted directory and configures an adapter.
 * Call close once when initialized. Never copy this struct after opening.
 * read_slot: reads a complete 64-byte snapshot or returns failure without
 * modifying the caller's output. Every attempted valid read invalidates and
 * scrubs the prior staged slot first; failed reads cannot be flushed or
 * committed using cached bytes. A missing slot reads as 64 zero bytes.
 * write_bytes cannot publish arbitrary or partial final-marker bytes:
 * it accepts ordinary writes only in 0..55, plus exactly eight zero bytes
 * at offset 56 for durable invalidation. publish_marker accepts only the
 * canonical ORECOM01 marker. These host-adapter constraints do NOT validate
 * caller authorization, prove real power-cut atomicity, or prevent rollback. */
int ores_iot_checkpoint_posix_open(ores_iot_checkpoint_posix *store,
                                   const char *trusted_directory);
void ores_iot_checkpoint_posix_close(ores_iot_checkpoint_posix *store);
ores_iot_checkpoint_io ores_iot_checkpoint_posix_adapter(
    ores_iot_checkpoint_posix *store);

#endif
