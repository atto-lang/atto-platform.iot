#ifndef ORES_IOT_STREAM_H
#define ORES_IOT_STREAM_H
#include "ores_iot_frame.h"
#ifdef __cplusplus
extern "C" {
#endif

/* Bounded stream decoder for IN-MEMORY trusted framing tests.
 * Security and peer authentication must be provided separately.
 */
#define ORES_IOT_MAX_FRAME_BYTES (ORES_IOT_HEADER_BYTES + ORES_IOT_MAX_PAYLOAD_BYTES)
#define ORES_IOT_MAX_FEED_BYTES 4096u

typedef struct {
    uint8_t buffer[ORES_IOT_MAX_FRAME_BYTES];
    size_t used;
    size_t expected;
    uint8_t busy;
    uint8_t failed;
} ores_iot_stream;

/* View borrows parser buffer and expires when callback returns. Do not reenter. */
typedef ores_iot_status (*ores_iot_frame_handler)(void *ctx, const ores_iot_view *view);

ores_iot_status ores_iot_stream_init(ores_iot_stream *stream);

/* Consumes fragmented/multiple frames, calls handler synchronously.
 * Invalid headers/callback errors poison the stream; it returns CLOSED until init.
 * Limit each call to 4096 bytes; caller must split longer reads.
 * Caller must not initialize/feed concurrently or during callbacks.
 * 'delivered' counts successfully handled frames, even on subsequent failure.
 */
ores_iot_status ores_iot_stream_feed(
    ores_iot_stream *stream, const uint8_t *chunk, size_t chunk_len,
    ores_iot_frame_handler handler, void *ctx, size_t *delivered);

#ifdef __cplusplus
}
#endif
#endif
