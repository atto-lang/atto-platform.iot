#include "ores_iot_replay.h"

/* Ensure the caller cannot restore bitmap entries corresponding to seq 0 or
 * negative sequence values. Strict comparison avoids 64-bit shifts. */
static int valid_checkpoint(uint64_t highest, uint64_t bitmap) {
    if (highest == 0) return bitmap == 0;
    if ((bitmap & UINT64_C(1)) == 0) return 0;
    if (highest < 64 && (bitmap >> highest) != 0) return 0;
    return 1;
}

ores_iot_replay_status ores_iot_replay_init(ores_iot_replay_window *window) {
    if (window == 0) return ORES_IOT_REPLAY_INVALID_ARGUMENT;
    window->highest = 0;
    window->bitmap = 0;
    return ORES_IOT_REPLAY_ACCEPT;
}

ores_iot_replay_status ores_iot_replay_restore(
    ores_iot_replay_window *window, uint64_t highest, uint64_t bitmap) {
    if (window == 0 || !valid_checkpoint(highest, bitmap))
        return ORES_IOT_REPLAY_INVALID_ARGUMENT;
    window->highest = highest;
    window->bitmap = bitmap;
    return ORES_IOT_REPLAY_ACCEPT;
}

ores_iot_replay_status ores_iot_replay_admit(
    ores_iot_replay_window *window, uint64_t sequence) {
    uint64_t delta, bit;
    if (window == 0 || !valid_checkpoint(window->highest, window->bitmap))
        return ORES_IOT_REPLAY_INVALID_ARGUMENT;
    if (sequence == 0) return ORES_IOT_REPLAY_INVALID_SEQUENCE;
    if (window->highest == 0) {
        window->highest = sequence;
        window->bitmap = UINT64_C(1);
        return ORES_IOT_REPLAY_ACCEPT;
    }
    if (sequence > window->highest) {
        delta = sequence - window->highest;
        window->bitmap = (delta >= 64 ? 0 : window->bitmap << delta) | UINT64_C(1);
        window->highest = sequence;
        return ORES_IOT_REPLAY_ACCEPT;
    }
    delta = window->highest - sequence;
    if (delta >= 64) return ORES_IOT_REPLAY_STALE;
    bit = UINT64_C(1) << delta;
    if (window->bitmap & bit) return ORES_IOT_REPLAY_DUPLICATE;
    window->bitmap |= bit;
    return ORES_IOT_REPLAY_ACCEPT;
}
