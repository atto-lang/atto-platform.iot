#include "ores_iot_stream.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdint.h>
#include <string.h>

/* Independent, deterministic fragmented-stream oracle, not authenticated transport. */
typedef struct {
    ores_iot_kind kind;
    size_t length;
    uint8_t payload[ORES_IOT_MAX_PAYLOAD_BYTES];
} expected_frame;

typedef struct {
    expected_frame frames[6];
    size_t next;
} oracle;

static ores_iot_status verify_frame(void *context, const ores_iot_view *view) {
    oracle *o = (oracle *)context;
    const expected_frame *f;
    assert(o->next < 6u);
    f = &o->frames[o->next];
    assert(view->kind == f->kind);
    assert(view->length == f->length);
    assert(view->payload != NULL);
    assert(memcmp(view->payload, f->payload, f->length) == 0);
    ++o->next;
    return ORES_IOT_OK;
}

static void fill_oracle(oracle *o) {
    static const size_t lengths[6] = {0u, 1u, 7u, 255u, 1024u, 3u};
    size_t i, j;
    memset(o, 0, sizeof(*o));
    for (i = 0; i < 6u; ++i) {
        o->frames[i].kind = (ores_iot_kind)(1u + i % 3u);
        o->frames[i].length = lengths[i];
        for (j = 0; j < lengths[i]; ++j)
            o->frames[i].payload[j] = (uint8_t)(i * 73u + j * 41u);
    }
}

static size_t make_wire(const oracle *o, uint8_t *buffer, size_t capacity,
                        size_t *after_two) {
    size_t i, cursor = 0;
    for (i = 0; i < 6u; ++i) {
        size_t written = 0;
        assert(ores_iot_encode(o->frames[i].kind, o->frames[i].payload,
            o->frames[i].length, buffer + cursor, capacity - cursor,
            &written) == ORES_IOT_OK);
        cursor += written;
        if (i == 1u) *after_two = cursor;
    }
    return cursor;
}

static void test_chunks(const uint8_t *wire, size_t length, size_t step) {
    ores_iot_stream stream;
    oracle o;
    size_t offset = 0;
    fill_oracle(&o);
    assert(step > 0 && step <= ORES_IOT_MAX_FEED_BYTES);
    assert(ores_iot_stream_init(&stream) == ORES_IOT_OK);
    while (offset < length) {
        size_t count = length - offset;
        size_t delivered = (size_t)-1;
        size_t before = o.next;
        if (count > step) count = step;
        assert(ores_iot_stream_feed(&stream, wire + offset, count,
            verify_frame, &o, &delivered) == ORES_IOT_OK);
        assert(delivered == o.next - before);
        offset += count;
    }
    assert(o.next == 6u);
    assert(ores_iot_stream_finish(&stream) == ORES_IOT_OK);
    assert(ores_iot_stream_finish(&stream) == ORES_IOT_CLOSED);
}

static void test_split(const uint8_t *wire, size_t length, size_t cut) {
    ores_iot_stream stream;
    oracle o;
    size_t n = (size_t)-1, first;
    fill_oracle(&o);
    assert(ores_iot_stream_init(&stream) == ORES_IOT_OK);
    assert(ores_iot_stream_feed(&stream, wire, cut,
        verify_frame, &o, &n) == ORES_IOT_OK);
    first = o.next;
    assert(n == first);
    assert(ores_iot_stream_feed(&stream, wire + cut, length - cut,
        verify_frame, &o, &n) == ORES_IOT_OK);
    assert(n == 6u - first);
    assert(o.next == 6u);
    assert(ores_iot_stream_finish(&stream) == ORES_IOT_OK);
}

int main(void) {
    uint8_t wire[6u * ORES_IOT_MAX_FRAME_BYTES + ORES_IOT_HEADER_BYTES];
    oracle model;
    size_t len, two = 0, n, i;
    ores_iot_stream stream;
    fill_oracle(&model);
    len = make_wire(&model, wire, sizeof wire, &two);
    assert(len < ORES_IOT_MAX_FEED_BYTES);
    for (i = 1; i <= 79u; ++i) test_chunks(wire, len, i);
    test_chunks(wire, len, ORES_IOT_MAX_FEED_BYTES);
    for (i = 0; i <= len; ++i) test_split(wire, len, i);

    /* Two successful callbacks followed by an invalid frame: both deliveries
     * must remain accounted for while the parser poisons its remaining state. */
    wire[two] = 'X';
    memcpy(wire + two + 1, "RES", 3u);
    wire[two + 4] = ORES_IOT_FRAME_VERSION;
    wire[two + 5] = ORES_IOT_TELEMETRY;
    wire[two + 6] = 0;
    wire[two + 7] = 0;
    fill_oracle(&model);
    assert(ores_iot_stream_init(&stream) == ORES_IOT_OK);
    assert(ores_iot_stream_feed(&stream, wire, two + ORES_IOT_HEADER_BYTES,
        verify_frame, &model, &n) == ORES_IOT_MALFORMED);
    assert(n == 2u && model.next == 2u && stream.failed == 1u);
    assert(ores_iot_stream_feed(&stream, wire, two,
        verify_frame, &model, &n) == ORES_IOT_CLOSED);
    assert(n == 0u);
    assert(ores_iot_stream_finish(&stream) == ORES_IOT_CLOSED);
    assert(ores_iot_stream_init(&stream) == ORES_IOT_OK);
    /* A truncated complete sequence must not be silently accepted at EOF. */
    fill_oracle(&model);
    len = make_wire(&model, wire, sizeof wire, &two);
    assert(ores_iot_stream_feed(&stream, wire, len - 1u,
        verify_frame, &model, &n) == ORES_IOT_OK);
    assert(n == 5u && model.next == 5u);
    assert(ores_iot_stream_finish(&stream) == ORES_IOT_MALFORMED);
    return 0;
}
