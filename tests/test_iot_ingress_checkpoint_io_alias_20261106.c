/* Host-only checkpoint callback-table storage isolation.
 * The ingress must reject callback tables overlapping any part of its
 * queue before clearing an unpublished slot or attempting checkpoint I/O.
 * No authentication, firmware, source-org CI or compiled guest claim.
 */
#include "ores_iot_actor_ingress.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>

_Static_assert(_Alignof(ores_iot_ingress_message) >=
               _Alignof(ores_iot_checkpoint_io),
               "queued messages must be aligned for this alias regression");
_Static_assert(sizeof(ores_iot_ingress_message) >=
               sizeof(ores_iot_checkpoint_io),
               "checkpoint table must fit within regression queue slot");

int main(void) {
    ores_iot_actor_ingress ingress;
    uint8_t before[sizeof ingress];
    uint8_t frame[ORES_IOT_MAX_FRAME_BYTES];
    const uint8_t payload[2] = {0x91u, 0x37u};
    size_t written = 0;
    unsigned slot;

    assert(ores_iot_encode(ORES_IOT_COMMAND, payload, sizeof payload,
                           frame, sizeof frame, &written) == ORES_IOT_OK);
    assert(written == ORES_IOT_HEADER_BYTES + sizeof payload);
    for (slot = 0; slot < ORES_IOT_INGRESS_QUEUE_CAPACITY; ++slot) {
        const ores_iot_checkpoint_io *alias;
        assert(ores_iot_actor_ingress_init(&ingress) == ORES_IOT_INGRESS_OK);
        memset(&ingress.queue[slot], 0xa5, sizeof ingress.queue[slot]);
        memcpy(before, &ingress, sizeof ingress);

        /* The cast is aligned and the table is never dereferenced: even
         * reading its callbacks would be unsafe after tail-slot scrub. */
        alias = (const ores_iot_checkpoint_io *)(const void *)&ingress.queue[slot];
        assert(ores_iot_actor_ingress_admit(&ingress, alias,
                   UINT64_C(1), UINT64_C(2), UINT64_C(3), 1,
                   frame, written) == ORES_IOT_INGRESS_INVALID_ARGUMENT);
        assert(memcmp(before, &ingress, sizeof ingress) == 0);
        assert(ores_iot_actor_ingress_count(&ingress) == 0u);
    }
    puts("IoT ingress checkpoint callback alias rejection: PASS");
    return 0;
}
