#include "ores_iot_replay.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#define CHECK_STATE(w, h, b) do { assert((w).highest == (h)); assert((w).bitmap == (b)); } while (0)

/* Independent reference model: stores each accepted sequence ID rather than
 * implementing the same bitmap-shift algorithm as the code under test. */
typedef struct { uint64_t highest; uint64_t values[64]; size_t count; } oracle;
static ores_iot_replay_status model_admit(oracle *m, uint64_t seq) {
    size_t i, n = 0;
    if (!seq) return ORES_IOT_REPLAY_INVALID_SEQUENCE;
    if (m->highest != 0 && seq <= m->highest && m->highest - seq >= 64)
        return ORES_IOT_REPLAY_STALE;
    for (i = 0; i < m->count; ++i)
        if (m->values[i] == seq) return ORES_IOT_REPLAY_DUPLICATE;
    if (seq > m->highest) m->highest = seq;
    for (i = 0; i < m->count; ++i)
        if (m->highest - m->values[i] < 64) m->values[n++] = m->values[i];
    m->count = n;
    assert(m->count < 64);
    m->values[m->count++] = seq;
    return ORES_IOT_REPLAY_ACCEPT;
}
static uint64_t model_bitmap(const oracle *m) {
    size_t i;
    uint64_t result = 0;
    for (i = 0; i < m->count; ++i)
        result |= UINT64_C(1) << (m->highest - m->values[i]);
    return result;
}
static uint64_t prng(uint64_t *state) {
    *state ^= *state << 13;
    *state ^= *state >> 7;
    *state ^= *state << 17;
    return *state;
}
int main(void) {
    ores_iot_replay_window w = {77, 3}, old;
    oracle m = {0};
    uint64_t state = UINT64_C(0x853c49e6748fea9b);
    size_t i, accepts = 0, dupes = 0, stale = 0;
    assert(ores_iot_replay_init(NULL) == ORES_IOT_REPLAY_INVALID_ARGUMENT);
    assert(ores_iot_replay_admit(NULL, 1) == ORES_IOT_REPLAY_INVALID_ARGUMENT);
    assert(ores_iot_replay_restore(NULL, 0, 0) == ORES_IOT_REPLAY_INVALID_ARGUMENT);
    assert(ores_iot_replay_restore(&w, 0, 1) == ORES_IOT_REPLAY_INVALID_ARGUMENT);
    CHECK_STATE(w, 77, 3);
    assert(ores_iot_replay_restore(&w, 2, UINT64_C(1) << 2) == ORES_IOT_REPLAY_INVALID_ARGUMENT);
    CHECK_STATE(w, 77, 3);
    assert(ores_iot_replay_restore(&w, 10, 2) == ORES_IOT_REPLAY_INVALID_ARGUMENT);
    CHECK_STATE(w, 77, 3);
    assert(ores_iot_replay_init(&w) == ORES_IOT_REPLAY_ACCEPT);
    CHECK_STATE(w, 0, 0);
    assert(ores_iot_replay_admit(&w, 0) == ORES_IOT_REPLAY_INVALID_SEQUENCE);
    CHECK_STATE(w, 0, 0);
    assert(ores_iot_replay_admit(&w, 10) == ORES_IOT_REPLAY_ACCEPT);
    assert(ores_iot_replay_admit(&w, 10) == ORES_IOT_REPLAY_DUPLICATE);
    assert(ores_iot_replay_admit(&w, 8) == ORES_IOT_REPLAY_ACCEPT);
    CHECK_STATE(w, 10, 5);
    old = w;
    assert(ores_iot_replay_admit(&w, 8) == ORES_IOT_REPLAY_DUPLICATE);
    CHECK_STATE(w, old.highest, old.bitmap);
    assert(ores_iot_replay_restore(&w, UINT64_MAX, 1) == ORES_IOT_REPLAY_ACCEPT);
    assert(ores_iot_replay_admit(&w, UINT64_MAX) == ORES_IOT_REPLAY_DUPLICATE);
    assert(ores_iot_replay_admit(&w, UINT64_MAX - 63) == ORES_IOT_REPLAY_ACCEPT);
    assert(ores_iot_replay_admit(&w, UINT64_MAX - 64) == ORES_IOT_REPLAY_STALE);
    assert(ores_iot_replay_admit(&w, 1) == ORES_IOT_REPLAY_STALE);
    assert(ores_iot_replay_restore(&w, 1, 1) == ORES_IOT_REPLAY_ACCEPT);
    assert(ores_iot_replay_admit(&w, 65) == ORES_IOT_REPLAY_ACCEPT);
    assert(ores_iot_replay_admit(&w, 1) == ORES_IOT_REPLAY_STALE);
    assert(ores_iot_replay_admit(&w, 2) == ORES_IOT_REPLAY_ACCEPT);
    assert(ores_iot_replay_restore(&w, 0, 0) == ORES_IOT_REPLAY_ACCEPT);
    /* Crash-safe caller sequencing: preview does not commit until persistence
     * succeeds. Failed admission must not alter the proposed output either. */
    {
        ores_iot_replay_window committed = {0, 0};
        ores_iot_replay_window next = {99, UINT64_C(0x55)};
        ores_iot_replay_window corrupt = {4, 0};
        assert(ores_iot_replay_preview(NULL, 1, &next) ==
               ORES_IOT_REPLAY_INVALID_ARGUMENT);
        assert(ores_iot_replay_preview(&committed, 1, NULL) ==
               ORES_IOT_REPLAY_INVALID_ARGUMENT);
        assert(ores_iot_replay_preview(&committed, 1, &committed) ==
               ORES_IOT_REPLAY_INVALID_ARGUMENT);
        assert(ores_iot_replay_preview(&corrupt, 1, &next) ==
               ORES_IOT_REPLAY_INVALID_ARGUMENT);
        assert(ores_iot_replay_preview(&committed, 0, &next) ==
               ORES_IOT_REPLAY_INVALID_SEQUENCE);
        CHECK_STATE(committed, 0, 0);
        CHECK_STATE(next, 99, UINT64_C(0x55));
        assert(ores_iot_replay_preview(&committed, 17, &next) ==
               ORES_IOT_REPLAY_ACCEPT);
        CHECK_STATE(next, 17, UINT64_C(1));
        CHECK_STATE(committed, 0, 0);
        /* Failed persistence can abandon next and safely retry the proposal. */
        assert(ores_iot_replay_preview(&committed, 17, &next) ==
               ORES_IOT_REPLAY_ACCEPT);
        assert(ores_iot_replay_restore(&committed, next.highest, next.bitmap) ==
               ORES_IOT_REPLAY_ACCEPT);
        assert(ores_iot_replay_preview(&committed, 17, &next) ==
               ORES_IOT_REPLAY_DUPLICATE);
        CHECK_STATE(committed, 17, UINT64_C(1));
        CHECK_STATE(next, 17, UINT64_C(1));
        assert(ores_iot_replay_preview(&committed, 1, &next) ==
               ORES_IOT_REPLAY_ACCEPT);
        CHECK_STATE(next, 17, UINT64_C(0x10001));
        CHECK_STATE(committed, 17, UINT64_C(1));
        assert(ores_iot_replay_restore(&committed, next.highest, next.bitmap) ==
               ORES_IOT_REPLAY_ACCEPT);
        assert(ores_iot_replay_preview(&committed, 1, &next) ==
               ORES_IOT_REPLAY_DUPLICATE);
        assert(ores_iot_replay_preview(&committed, UINT64_MAX, &next) ==
               ORES_IOT_REPLAY_ACCEPT);
        CHECK_STATE(next, UINT64_MAX, UINT64_C(1));
        CHECK_STATE(committed, 17, UINT64_C(0x10001));
    }
    for (i = 0; i < 40000; ++i) {
        uint64_t r = prng(&state), seq;
        ores_iot_replay_status expected, actual;
        if (i % 41 == 0) seq = 0;
        else if (i % 7 == 0 && m.highest) seq = m.highest + 1 + (r % 19);
        else if (i % 97 == 0) seq = (m.highest > 100 ? m.highest - 100 : 1);
        else if (i % 11 == 0 && m.highest) seq = m.highest;
        else if (i % 3 == 0 && m.highest > 1) seq = m.highest - (r % 128);
        else seq = (m.highest ? m.highest : 100) + (r % 31) - 15;
        expected = model_admit(&m, seq);
        {
            ores_iot_replay_window candidate = {UINT64_MAX, UINT64_MAX};
            ores_iot_replay_window prior = w;
            ores_iot_replay_status preview =
                ores_iot_replay_preview(&w, seq, &candidate);
            assert(preview == expected);
            assert(w.highest == prior.highest && w.bitmap == prior.bitmap);
            actual = ores_iot_replay_admit(&w, seq);
            assert(actual == expected);
            if (preview == ORES_IOT_REPLAY_ACCEPT) {
                assert(candidate.highest == w.highest &&
                       candidate.bitmap == w.bitmap);
            } else {
                CHECK_STATE(candidate, UINT64_MAX, UINT64_MAX);
            }
        }
        if (actual == ORES_IOT_REPLAY_ACCEPT) ++accepts;
        if (actual == ORES_IOT_REPLAY_DUPLICATE) ++dupes;
        if (actual == ORES_IOT_REPLAY_STALE) ++stale;
        assert(w.highest == m.highest && w.bitmap == model_bitmap(&m));
        if (i % 257 == 0) {
            ores_iot_replay_window restored = {0, 0};
            assert(ores_iot_replay_restore(&restored, w.highest, w.bitmap) == ORES_IOT_REPLAY_ACCEPT);
            CHECK_STATE(restored, w.highest, w.bitmap);
        }
    }
    assert(accepts > 1000 && dupes > 100 && stale > 100);
    /* A corrupted checkpoint must not become a valid in-memory state. */
    w.bitmap = 0;
    assert(ores_iot_replay_admit(&w, 23) == ORES_IOT_REPLAY_INVALID_ARGUMENT);
    return 0;
}
