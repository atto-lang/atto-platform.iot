#ifndef ORES_IOT_FRAME_H
#define ORES_IOT_FRAME_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Strict binary framing reference, NOT authenticated transport. */
#define ORES_IOT_FRAME_VERSION 1u
#define ORES_IOT_HEADER_BYTES 8u
#define ORES_IOT_MAX_PAYLOAD_BYTES 1024u

typedef enum {
    ORES_IOT_OK = 0,
    ORES_IOT_INVALID_ARGUMENT = 1,
    ORES_IOT_TOO_LARGE = 2,
    ORES_IOT_BUFFER_TOO_SMALL = 3,
    ORES_IOT_MALFORMED = 4,
    ORES_IOT_UNSUPPORTED_VERSION = 5,
    ORES_IOT_UNSUPPORTED_KIND = 6,
    ORES_IOT_CLOSED = 7 /* stream failed and must be reinitialized */
} ores_iot_status;

typedef enum {
    ORES_IOT_TELEMETRY = 1,
    ORES_IOT_COMMAND = 2,
    ORES_IOT_ACK = 3
} ores_iot_kind;

/* The payload is BORROWED from input frame: no copying or ownership transfer. */
typedef struct {
    ores_iot_kind kind;
    const uint8_t *payload;
    size_t length;
} ores_iot_view;

/* Encode into caller-owned output. Input may overlap output; written must not alias
 * the payload or destination byte ranges. Aliasing is rejected before any write.
 * A non-aliasing written count is zeroed on every failure, including invalid
 * kinds, oversized messages and NULL byte pointers. */
ores_iot_status ores_iot_encode(ores_iot_kind kind,
    const uint8_t *payload, size_t payload_len,
    uint8_t *destination, size_t capacity, size_t *written);

/* Exact-length, fail-closed validation. View valid while input frame lives.
 * The output view object must not overlap the input frame bytes. */
ores_iot_status ores_iot_decode(const uint8_t *frame,
    size_t frame_len, ores_iot_view *out);

#ifdef __cplusplus
}
#endif
#endif

