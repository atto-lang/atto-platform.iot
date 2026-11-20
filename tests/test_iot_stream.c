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
static ores_iot_status chosen_status(void *ctx, const ores_iot_view *view) {
    assert(view->length == 2 && view->payload[0] == 7 && view->payload[1] == 8);
    return *(const ores_iot_status *)ctx;
}
static void assert_scrubbed(const ores_iot_stream *stream) {
    size_t i;
    for (i = 0; i < sizeof stream->buffer; ++i) assert(stream->buffer[i] == 0);
}
static void test_scrub_and_status(void) {
    ores_iot_stream stream;
    uint8_t frame[2 * ORES_IOT_MAX_FRAME_BYTES], payload[] = {7, 8};
    const ores_iot_status results[] = {
        ORES_IOT_OK, ORES_IOT_INVALID_ARGUMENT, ORES_IOT_TOO_LARGE,
        ORES_IOT_BUFFER_TOO_SMALL, ORES_IOT_MALFORMED,
        ORES_IOT_UNSUPPORTED_VERSION, ORES_IOT_UNSUPPORTED_KIND,
        ORES_IOT_CLOSED, (ores_iot_status)99, (ores_iot_status)-1
    };
    size_t size, delivered, i;
    assert(ores_iot_encode(ORES_IOT_TELEMETRY, payload, sizeof payload,
                           frame, sizeof frame, &size) == ORES_IOT_OK);
    for (i = 0; i < sizeof results / sizeof results[0]; ++i) {
        ores_iot_status result = results[i];
        assert(ores_iot_stream_init(&stream) == ORES_IOT_OK);
        assert(ores_iot_stream_feed(&stream, frame, size, chosen_status,
            &result, &delivered) == (i < 8 ? result : ORES_IOT_INVALID_ARGUMENT));
        assert(delivered == (i == 0 ? 1u : 0u));
        assert_scrubbed(&stream);
        if (i != 0) assert(ores_iot_stream_finish(&stream) == ORES_IOT_CLOSED);
    }
    /* A successful frame followed by a malformed one retains the good count. */
    memcpy(frame + size, frame, size);
    frame[size] = 'X';
    {
        ores_iot_status result = ORES_IOT_OK;
        assert(ores_iot_stream_init(&stream) == ORES_IOT_OK);
        assert(ores_iot_stream_feed(&stream, frame, 2 * size, chosen_status,
                                   &result, &delivered) == ORES_IOT_MALFORMED);
        assert(delivered == 1 && stream.failed);
        assert_scrubbed(&stream);
        /* Every partial header/payload must survive feed and erase at EOF. */
        for (i = 1; i < size; ++i) {
            assert(ores_iot_stream_init(&stream) == ORES_IOT_OK);
            assert(ores_iot_stream_feed(&stream, frame, i, chosen_status,
                                       &result, &delivered) == ORES_IOT_OK);
            assert(stream.buffer[0] == 'O' && stream.used == i);
            assert(ores_iot_stream_finish(&stream) == ORES_IOT_MALFORMED);
            assert_scrubbed(&stream);
        }
    }
}
static void test_corrupt_stream_state_fails_closed(void) {
    ores_iot_stream stream;
    uint8_t frame[ORES_IOT_MAX_FRAME_BYTES]={0};
    size_t delivered=99;
    stats s={0,0};

    assert(ores_iot_stream_init(&stream)==ORES_IOT_OK);
    stream.used=ORES_IOT_MAX_FRAME_BYTES+1u;
    assert(ores_iot_stream_feed(&stream,NULL,0,callback,&s,&delivered)
           ==ORES_IOT_INVALID_ARGUMENT);
    assert(delivered==0);
    assert(ores_iot_stream_finish(&stream)==ORES_IOT_INVALID_ARGUMENT);

    assert(ores_iot_stream_init(&stream)==ORES_IOT_OK);
    stream.expected=ORES_IOT_MAX_FRAME_BYTES+1u;
    delivered=99;
    assert(ores_iot_stream_feed(&stream,NULL,0,callback,&s,&delivered)
           ==ORES_IOT_INVALID_ARGUMENT);
    assert(delivered==0);

    assert(ores_iot_stream_init(&stream)==ORES_IOT_OK);
    stream.used=ORES_IOT_HEADER_BYTES+1u;
    stream.expected=0;
    delivered=99;
    assert(ores_iot_stream_feed(&stream,NULL,0,callback,&s,&delivered)
           ==ORES_IOT_INVALID_ARGUMENT);
    assert(delivered==0);

    assert(ores_iot_stream_init(&stream)==ORES_IOT_OK);
    stream.used=ORES_IOT_HEADER_BYTES;
    stream.expected=ORES_IOT_HEADER_BYTES-1u;
    delivered=99;
    assert(ores_iot_stream_feed(&stream,NULL,0,callback,&s,&delivered)
           ==ORES_IOT_INVALID_ARGUMENT);
    assert(delivered==0);

    assert(ores_iot_stream_init(&stream)==ORES_IOT_OK);
    stream.failed=1;
    stream.used=1;
    assert(ores_iot_stream_finish(&stream)==ORES_IOT_INVALID_ARGUMENT);

    assert(ores_iot_stream_init(&stream)==ORES_IOT_OK);
    stream.busy=2;
    delivered=99;
    assert(ores_iot_stream_feed(&stream,frame,0,callback,&s,&delivered)
           ==ORES_IOT_INVALID_ARGUMENT);
    assert(delivered==0);
}

int main(void) {
    test_scrub_and_status();
    test_corrupt_stream_state_fails_closed();
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
    /* Reused counters must not leak a stale positive delivery count after
     * rejected calls. The stream remains usable after invalid arguments. */
    delivered = 99;
    assert(ores_iot_stream_feed(&stream, NULL, 1, callback, &s, &delivered) == ORES_IOT_INVALID_ARGUMENT);
    assert(delivered == 0 && stream.failed == 0 && stream.used == 0);
    delivered = 99;
    assert(ores_iot_stream_feed(&stream, frame, size, NULL, &s, &delivered) == ORES_IOT_INVALID_ARGUMENT);
    assert(delivered == 0 && stream.failed == 0);
    delivered = 99;
    assert(ores_iot_stream_feed(NULL, frame, size, callback, &s, &delivered) == ORES_IOT_INVALID_ARGUMENT);
    assert(delivered == 0);
    assert(ores_iot_stream_feed(&stream, frame, size, callback, &s, NULL) == ORES_IOT_INVALID_ARGUMENT);
    assert(ores_iot_stream_feed(&stream, frame, size, callback, &s, &delivered) == ORES_IOT_OK);
    assert(delivered == 1 && s.count == 5);
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

