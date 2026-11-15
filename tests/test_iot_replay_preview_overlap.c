#include "ores_iot_replay.h"
#include <assert.h>
#include <stdint.h>
#include <string.h>

/* Overlap pointers are never dereferenced by the caller or the API. */
int main(void) {
    uint64_t region[3] = {11u, 22u, 33u};
    uint64_t before[3];
    ores_iot_replay_window committed = {100u, 1u};
    ores_iot_replay_window candidate = {77u, 55u};
    memcpy(before, region, sizeof(region));
    assert(ores_iot_replay_preview(
        (const ores_iot_replay_window *)(const void *)&region[0], 200u,
        (ores_iot_replay_window *)(void *)&region[1])
        == ORES_IOT_REPLAY_INVALID_ARGUMENT);
    assert(memcmp(before, region, sizeof(region)) == 0);
    assert(ores_iot_replay_preview(
        (const ores_iot_replay_window *)(const void *)&region[1], 200u,
        (ores_iot_replay_window *)(void *)&region[0])
        == ORES_IOT_REPLAY_INVALID_ARGUMENT);
    assert(memcmp(before, region, sizeof(region)) == 0);
    assert(ores_iot_replay_preview(&committed, 200u, &committed)
        == ORES_IOT_REPLAY_INVALID_ARGUMENT);
    assert(committed.highest == 100u && committed.bitmap == 1u);
    assert(ores_iot_replay_preview(&committed, 100u, &candidate)
        == ORES_IOT_REPLAY_DUPLICATE);
    assert(candidate.highest == 77u && candidate.bitmap == 55u);
    assert(ores_iot_replay_preview(&committed, 200u, &candidate)
        == ORES_IOT_REPLAY_ACCEPT);
    assert(candidate.highest == 200u && candidate.bitmap == 1u);
    assert(committed.highest == 100u && committed.bitmap == 1u);
    return 0;
}
