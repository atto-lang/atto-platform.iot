#include "ores_iot_replay.h"
#include <assert.h>
#include <stdint.h>

int main(void) {
    ores_iot_replay_window committed = {0, 0};
    ores_iot_replay_window saved;
    assert(ores_iot_replay_classify(0, 1) == ORES_IOT_REPLAY_INVALID_ARGUMENT);
    assert(ores_iot_replay_classify(&committed, 0) == ORES_IOT_REPLAY_INVALID_SEQUENCE);
    assert(ores_iot_replay_classify(&committed, 1) == ORES_IOT_REPLAY_ACCEPT);
    assert(committed.highest == 0 && committed.bitmap == 0);
    assert(ores_iot_replay_admit(&committed, 100) == ORES_IOT_REPLAY_ACCEPT);
    saved = committed;
    assert(ores_iot_replay_classify(&committed, 100) == ORES_IOT_REPLAY_DUPLICATE);
    assert(ores_iot_replay_classify(&committed, 99) == ORES_IOT_REPLAY_ACCEPT);
    assert(ores_iot_replay_classify(&committed, 36) == ORES_IOT_REPLAY_STALE);
    assert(ores_iot_replay_classify(&committed, UINT64_MAX) == ORES_IOT_REPLAY_ACCEPT);
    assert(ores_iot_replay_classify(&committed, 0) == ORES_IOT_REPLAY_INVALID_SEQUENCE);
    assert(committed.highest == saved.highest && committed.bitmap == saved.bitmap);
    committed.bitmap = 0; /* invalid restored checkpoint fails closed */
    assert(ores_iot_replay_classify(&committed, 101) == ORES_IOT_REPLAY_INVALID_ARGUMENT);
    assert(committed.highest == 100 && committed.bitmap == 0);
    return 0;
}
