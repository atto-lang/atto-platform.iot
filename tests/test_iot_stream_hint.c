#include "ores_iot_stream.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>

static ores_iot_status on_frame(void *ctx, const ores_iot_view *view) {
    unsigned *count = (unsigned *)ctx;
    assert(view->kind == ORES_IOT_COMMAND);
    assert(view->length == 2);
    assert(view->payload[0] == 0xa5 && view->payload[1] == 0x5a);
    ++*count;
    return ORES_IOT_OK;
}

int main(void) {
    ores_iot_stream stream;
    uint8_t frame[] = {'O', 'R', 'E', 'S', ORES_IOT_FRAME_VERSION,
                       ORES_IOT_COMMAND, 0, 2, 0xa5, 0x5a};
    size_t hint = 999, delivered = 999;
    unsigned count = 0;
    assert(ores_iot_stream_init(&stream) == ORES_IOT_OK);
    assert(ores_iot_stream_bytes_needed(&stream, &hint) == ORES_IOT_OK);
    assert(hint == ORES_IOT_HEADER_BYTES);
    assert(ores_iot_stream_feed(&stream, frame, 3, on_frame, &count, &delivered)
           == ORES_IOT_OK && delivered == 0);
    assert(ores_iot_stream_bytes_needed(&stream, &hint) == ORES_IOT_OK);
    assert(hint == ORES_IOT_HEADER_BYTES - 3);
    assert(ores_iot_stream_feed(&stream, frame + 3, 5, on_frame, &count, &delivered)
           == ORES_IOT_OK && delivered == 0);
    assert(ores_iot_stream_bytes_needed(&stream, &hint) == ORES_IOT_OK);
    assert(hint == 2);
    assert(ores_iot_stream_feed(&stream, frame + 8, 1, on_frame, &count, &delivered)
           == ORES_IOT_OK && delivered == 0);
    assert(ores_iot_stream_bytes_needed(&stream, &hint) == ORES_IOT_OK);
    assert(hint == 1);
    assert(ores_iot_stream_feed(&stream, frame + 9, 1, on_frame, &count, &delivered)
           == ORES_IOT_OK && delivered == 1 && count == 1);
    assert(ores_iot_stream_bytes_needed(&stream, &hint) == ORES_IOT_OK);
    assert(hint == ORES_IOT_HEADER_BYTES);

    assert(ores_iot_stream_bytes_needed(&stream, &stream.used)
           == ORES_IOT_INVALID_ARGUMENT);
    assert(stream.used == 0);
    stream.busy = 1;
    hint = 999;
    assert(ores_iot_stream_bytes_needed(&stream, &hint)
           == ORES_IOT_INVALID_ARGUMENT && hint == 0);
    stream.busy = 0;
    assert(ores_iot_stream_finish(&stream) == ORES_IOT_OK);
    hint = 999;
    assert(ores_iot_stream_bytes_needed(&stream, &hint) == ORES_IOT_CLOSED);
    assert(hint == 0);
    hint = 999;
    assert(ores_iot_stream_bytes_needed(NULL, &hint)
           == ORES_IOT_INVALID_ARGUMENT && hint == 0);
    return 0;
}
