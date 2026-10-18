#include "ores_iot_stream.h"
#ifdef NDEBUG
#undef NDEBUG /* Release builds must execute test assertions */
#endif
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct { size_t count; uint8_t last; } stats;
static ores_iot_status callback(void *ctx, const ores_iot_view *view) {
    stats *s = (stats *)ctx;
    assert(view->kind == ORES_IOT_TELEMETRY);
    s->count++;
    s->last = view->length ? view->payload[0] : 0;
    return ORES_IOT_OK;
}
static ores_iot_status failure(void *ctx, const ores_iot_view *view) {
    (void)ctx; (void)view;
    return ORES_IOT_INVALID_ARGUMENT;
}
static ores_iot_status reinitialize(void *ctx, const ores_iot_view *view) {
    (void)view;
    return ores_iot_stream_init((ores_iot_stream *)ctx);
}
int main(void) {
    ores_iot_stream stream;
    uint8_t frame[ORES_IOT_MAX_FRAME_BYTES], pair[2 * ORES_IOT_MAX_FRAME_BYTES];
    const uint8_t data[] = {7u, 8u};
    size_t size = 0, delivered = 999;
    stats s = {0, 0};
    assert(ores_iot_stream_init(NULL) == ORES_IOT_INVALID_ARGUMENT);
    assert(ores_iot_stream_init(&stream) == ORES_IOT_OK);
    assert(ores_iot_encode(ORES_IOT_TELEMETRY, data, sizeof data,
                           frame, sizeof frame, &size) == ORES_IOT_OK);
    assert(ores_iot_stream_feed(&stream, frame, 4, callback, &s, &delivered) == ORES_IOT_OK);
    assert(delivered == 0 && s.count == 0);
    assert(ores_iot_stream_feed(&stream, frame + 4, size - 4, callback, &s, &delivered) == ORES_IOT_OK);
    assert(delivered == 1 && s.count == 1 && s.last == 7);
    memcpy(pair, frame, size);
    memcpy(pair + size, frame, size);
    assert(ores_iot_stream_feed(&stream, pair, size * 2, callback, &s, &delivered) == ORES_IOT_OK);
    assert(delivered == 2 && s.count == 3);
    assert(ores_iot_stream_feed(&stream, frame, size, failure, NULL, &delivered) == ORES_IOT_INVALID_ARGUMENT);
    assert(delivered == 0 && stream.used == 0 && stream.failed == 1);
    assert(ores_iot_stream_feed(&stream, frame, size, callback, &s, &delivered) == ORES_IOT_CLOSED);
    assert(ores_iot_stream_init(&stream) == ORES_IOT_OK);
    frame[0] = 'X';
    assert(ores_iot_stream_feed(&stream, frame, size, callback, &s, &delivered) == ORES_IOT_MALFORMED);
    assert(delivered == 0 && stream.used == 0 && stream.failed == 1);
    frame[0] = 'O';
    assert(ores_iot_stream_feed(&stream, frame, size, callback, &s, &delivered) == ORES_IOT_CLOSED);
    assert(ores_iot_stream_init(&stream) == ORES_IOT_OK);
    assert(ores_iot_stream_feed(&stream, frame, size, callback, &s, &delivered) == ORES_IOT_OK);
    assert(delivered == 1 && s.count == 4);
    assert(ores_iot_stream_feed(&stream, NULL, 1, callback, &s, &delivered) == ORES_IOT_INVALID_ARGUMENT);
    assert(ores_iot_stream_feed(&stream, frame, size, NULL, &s, &delivered) == ORES_IOT_INVALID_ARGUMENT);
    assert(ores_iot_stream_feed(&stream, frame, size, callback, &s, NULL) == ORES_IOT_INVALID_ARGUMENT);
    assert(ores_iot_stream_feed(&stream, stream.buffer, 1, callback, &s, &delivered) == ORES_IOT_INVALID_ARGUMENT);
    assert(ores_iot_stream_feed(&stream, frame, size, callback, &s, &stream.used) == ORES_IOT_INVALID_ARGUMENT);
    {
        static uint8_t oversized[ORES_IOT_MAX_FEED_BYTES + 1u];
        assert(ores_iot_stream_feed(&stream, oversized, sizeof oversized,
                                   callback, &s, &delivered) == ORES_IOT_TOO_LARGE);
        assert(delivered == 0 && stream.used == 0 && stream.failed == 0);
    }
    stream.busy = 1;
    assert(ores_iot_stream_feed(&stream, frame, size, callback, &s, &delivered) == ORES_IOT_INVALID_ARGUMENT);
    assert(ores_iot_stream_finish(&stream) == ORES_IOT_INVALID_ARGUMENT);
    assert(ores_iot_stream_finish(NULL) == ORES_IOT_INVALID_ARGUMENT);
    assert(ores_iot_stream_init(&stream) == ORES_IOT_OK);
    assert(ores_iot_stream_finish(&stream) == ORES_IOT_OK);
    assert(ores_iot_stream_finish(&stream) == ORES_IOT_CLOSED);
    assert(ores_iot_stream_feed(&stream, frame, size, callback, &s, &delivered) == ORES_IOT_CLOSED);
    {
        size_t prefix;
        for (prefix = 1; prefix < size; ++prefix) {
            assert(ores_iot_stream_init(&stream) == ORES_IOT_OK);
            assert(ores_iot_stream_feed(&stream, frame, prefix, callback, &s, &delivered) == ORES_IOT_OK);
            assert(delivered == 0);
            assert(ores_iot_stream_finish(&stream) == ORES_IOT_MALFORMED);
            assert(stream.used == 0 && stream.expected == 0);
            assert(ores_iot_stream_finish(&stream) == ORES_IOT_CLOSED);
        }
    }
    assert(ores_iot_stream_init(&stream) == ORES_IOT_OK);
    assert(ores_iot_stream_feed(&stream, frame, size, reinitialize, &stream, &delivered) == ORES_IOT_INVALID_ARGUMENT);
    assert(delivered == 0 && stream.failed == 1 && stream.busy == 0);
    assert(ores_iot_stream_feed(&stream, frame, size, callback, &s, &delivered) == ORES_IOT_CLOSED);
    assert(ores_iot_stream_init(&stream) == ORES_IOT_OK);
    assert(ores_iot_stream_feed(&stream, frame, size, callback, &s, &delivered) == ORES_IOT_OK);
    assert(delivered == 1);
    assert(ores_iot_stream_finish(&stream) == ORES_IOT_OK);
    return 0;
}
