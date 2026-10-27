#include "ores_iot_stream.h"
#include <stdint.h>

/* CPU-side erasure only; the callback borrow has ended before this runs. */
static void scrub_buffer(ores_iot_stream *stream) {
    volatile uint8_t *p = stream->buffer;
    size_t i;
    for (i = 0; i < ORES_IOT_MAX_FRAME_BYTES; ++i) p[i] = 0;
}

static int overlaps_region(const void *left, size_t left_len,
                           const void *right, size_t right_len) {
    uintptr_t a, b;
    if (left == NULL || right == NULL || left_len == 0 || right_len == 0) return 0;
    a = (uintptr_t)left;
    b = (uintptr_t)right;
    return a >= b ? a - b < right_len : b - a < left_len;
}

static int valid_stream_state(const ores_iot_stream *stream) {
    if(stream==NULL ||
       stream->used>ORES_IOT_MAX_FRAME_BYTES ||
       stream->expected>ORES_IOT_MAX_FRAME_BYTES ||
       stream->busy>1u || stream->failed>1u)
        return 0;
    if(stream->failed)
        return stream->used==0 && stream->expected==0;
    if(stream->expected==0)
        return stream->used<ORES_IOT_HEADER_BYTES;
    if(stream->expected<ORES_IOT_HEADER_BYTES ||
       stream->used<ORES_IOT_HEADER_BYTES ||
       stream->used>stream->expected)
        return 0;
    return 1;
}

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
    if (stream == NULL) return ORES_IOT_INVALID_ARGUMENT;
    scrub_buffer(stream);
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
    /* Reject conflicting output storage before any write, but clear a safe
     * count on every other entry path (including invalid input arguments). */
    if (overlaps_region(stream, sizeof(*stream), delivered, sizeof(*delivered)) ||
        overlaps_region(chunk, chunk_len, delivered, sizeof(*delivered)))
        return ORES_IOT_INVALID_ARGUMENT;
    *delivered = 0;
    if (stream == NULL || handler == NULL || (chunk_len != 0 && chunk == NULL))
        return ORES_IOT_INVALID_ARGUMENT;
    if (overlaps_region(stream, sizeof(*stream), chunk, chunk_len))
        return ORES_IOT_INVALID_ARGUMENT;
    if (!valid_stream_state(stream) || stream->busy)
        return ORES_IOT_INVALID_ARGUMENT;
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
            size_t expected = stream->expected;
            status = ores_iot_decode(stream->buffer, stream->used, &view);
            if (status != ORES_IOT_OK) break;
            status = handler(ctx, &view);
            if (status < ORES_IOT_OK || status > ORES_IOT_CLOSED)
                status = ORES_IOT_INVALID_ARGUMENT;
            if (status != ORES_IOT_OK) break;
            if (!stream->busy || stream->failed ||
                stream->used != expected || stream->expected != expected) {
                status = ORES_IOT_INVALID_ARGUMENT;
                break;
            }
            ++(*delivered);
            scrub_buffer(stream);
            stream->used = 0;
            stream->expected = 0;
        }
    }
    if (status != ORES_IOT_OK) {
        scrub_buffer(stream);
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
    if (!valid_stream_state(stream) || stream->busy)
        return ORES_IOT_INVALID_ARGUMENT;
    if (stream->failed) return ORES_IOT_CLOSED;
    status = stream->used == 0 && stream->expected == 0 ? ORES_IOT_OK : ORES_IOT_MALFORMED;
    scrub_buffer(stream);
    stream->used = 0;
    stream->expected = 0;
    stream->failed = 1;
    return status;
}

