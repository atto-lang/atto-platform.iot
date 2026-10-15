#include "ores_iot_stream.h"

static ores_iot_status inspect_header(const uint8_t *p, size_t *expected) {
    size_t length;
    if (p[0] != 'O' || p[1] != 'R' || p[2] != 'E' || p[3] != 'S')
        return ORES_IOT_MALFORMED;
    if (p[4] != ORES_IOT_FRAME_VERSION) return ORES_IOT_UNSUPPORTED_VERSION;
    if (p[5] != ORES_IOT_TELEMETRY &&
        p[5] != ORES_IOT_COMMAND && p[5] != ORES_IOT_ACK)
        return ORES_IOT_UNSUPPORTED_KIND;
    length = ((size_t)p[6] << 8) | (size_t)p[7];
    if (length > ORES_IOT_MAX_PAYLOAD_BYTES) return ORES_IOT_TOO_LARGE;
    *expected = ORES_IOT_HEADER_BYTES + length;
    return ORES_IOT_OK;
}

ores_iot_status ores_iot_stream_init(ores_iot_stream *stream) {
    size_t i;
    if (stream == NULL) return ORES_IOT_INVALID_ARGUMENT;
    for (i = 0; i < ORES_IOT_MAX_FRAME_BYTES; ++i) stream->buffer[i] = 0;
    stream->used = 0;
    stream->expected = 0;
    stream->busy = 0;
    stream->failed = 0;
    return ORES_IOT_OK;
}

ores_iot_status ores_iot_stream_feed(
    ores_iot_stream *stream, const uint8_t *chunk, size_t chunk_len,
    ores_iot_frame_handler handler, void *ctx, size_t *delivered) {
    size_t i;
    ores_iot_status status = ORES_IOT_OK;
    if (delivered == NULL) return ORES_IOT_INVALID_ARGUMENT;
    *delivered = 0;
    if (stream == NULL || handler == NULL || (chunk_len != 0 && chunk == NULL))
        return ORES_IOT_INVALID_ARGUMENT;
    if (stream->busy) return ORES_IOT_INVALID_ARGUMENT;
    if (stream->failed) return ORES_IOT_CLOSED;
    if (chunk_len > ORES_IOT_MAX_FEED_BYTES) return ORES_IOT_TOO_LARGE;
    stream->busy = 1;
    for (i = 0; i < chunk_len; ++i) {
        ores_iot_view view;
        if (stream->used >= ORES_IOT_MAX_FRAME_BYTES) {
            status = ORES_IOT_TOO_LARGE;
            break;
        }
        stream->buffer[stream->used++] = chunk[i];
        if (stream->used == ORES_IOT_HEADER_BYTES && stream->expected == 0) {
            status = inspect_header(stream->buffer, &stream->expected);
            if (status != ORES_IOT_OK) break;
        }
        if (stream->expected != 0 && stream->used == stream->expected) {
            status = ores_iot_decode(stream->buffer, stream->used, &view);
            if (status != ORES_IOT_OK) break;
            status = handler(ctx, &view);
            if (status != ORES_IOT_OK) break;
            ++(*delivered);
            stream->used = 0;
            stream->expected = 0;
        }
    }
    if (status != ORES_IOT_OK) {
        /* Reset to prevent accepting tail of a malformed frame as a header. */
        stream->used = 0;
        stream->expected = 0;
        stream->failed = 1;
    }
    stream->busy = 0;
    return status;
}

ores_iot_status ores_iot_stream_finish(ores_iot_stream *stream) {
    ores_iot_status status;
    if (stream == NULL || stream->busy) return ORES_IOT_INVALID_ARGUMENT;
    if (stream->failed) return ORES_IOT_CLOSED;
    status = stream->used == 0 && stream->expected == 0 ? ORES_IOT_OK : ORES_IOT_MALFORMED;
    stream->used = 0;
    stream->expected = 0;
    stream->failed = 1;
    return status;
}
