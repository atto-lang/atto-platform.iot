#include "ores_iot_frame.h"
#include <stdint.h>
#include <string.h>

static int valid_kind(unsigned k) {
    return k == ORES_IOT_TELEMETRY || k == ORES_IOT_COMMAND || k == ORES_IOT_ACK;
}

static int overlaps_region(const void *left, size_t left_len,
                           const void *right, size_t right_len) {
    uintptr_t a, b;
    if (left == NULL || right == NULL || left_len == 0 || right_len == 0) return 0;
    a = (uintptr_t)left;
    b = (uintptr_t)right;
    return a >= b ? a - b < right_len : b - a < left_len;
}

ores_iot_status ores_iot_encode(ores_iot_kind kind,
    const uint8_t *payload, size_t payload_len,
    uint8_t *destination, size_t capacity, size_t *written) {
    size_t total;
    if (written == NULL) return ORES_IOT_INVALID_ARGUMENT;
    if (!valid_kind((unsigned)kind)) return ORES_IOT_UNSUPPORTED_KIND;
    if (payload_len > ORES_IOT_MAX_PAYLOAD_BYTES) return ORES_IOT_TOO_LARGE;
    if ((payload_len != 0 && payload == NULL) || destination == NULL)
        return ORES_IOT_INVALID_ARGUMENT;
    if (overlaps_region(payload, payload_len, written, sizeof(*written)) ||
        overlaps_region(destination, capacity, written, sizeof(*written)))
        return ORES_IOT_INVALID_ARGUMENT;
    *written = 0;
    total = ORES_IOT_HEADER_BYTES + payload_len;
    if (capacity < total) return ORES_IOT_BUFFER_TOO_SMALL;
    /* Copy before writing the header: overlapping input/output is supported. */
    if (payload_len != 0) memmove(destination + ORES_IOT_HEADER_BYTES, payload, payload_len);
    destination[0] = 'O'; destination[1] = 'R';
    destination[2] = 'E'; destination[3] = 'S';
    destination[4] = (uint8_t)ORES_IOT_FRAME_VERSION;
    destination[5] = (uint8_t)kind;
    destination[6] = (uint8_t)(payload_len >> 8);
    destination[7] = (uint8_t)(payload_len & 255u);
    *written = total;
    return ORES_IOT_OK;
}

ores_iot_status ores_iot_decode(const uint8_t *frame,
    size_t frame_len, ores_iot_view *out) {
    size_t payload_len;
    if (out == NULL) return ORES_IOT_INVALID_ARGUMENT;
    if (frame == NULL) {
        out->kind = (ores_iot_kind)0;
        out->payload = NULL;
        out->length = 0;
        return ORES_IOT_INVALID_ARGUMENT;
    }
    if (overlaps_region(frame, frame_len, out, sizeof(*out)))
        return ORES_IOT_INVALID_ARGUMENT;
    out->kind = (ores_iot_kind)0;
    out->payload = NULL;
    out->length = 0;
    if (frame_len < ORES_IOT_HEADER_BYTES) return ORES_IOT_MALFORMED;
    if (frame[0] != 'O' || frame[1] != 'R' || frame[2] != 'E' || frame[3] != 'S')
        return ORES_IOT_MALFORMED;
    if (frame[4] != ORES_IOT_FRAME_VERSION) return ORES_IOT_UNSUPPORTED_VERSION;
    if (!valid_kind(frame[5])) return ORES_IOT_UNSUPPORTED_KIND;
    payload_len = ((size_t)frame[6] << 8) | (size_t)frame[7];
    if (payload_len > ORES_IOT_MAX_PAYLOAD_BYTES) return ORES_IOT_TOO_LARGE;
    if (frame_len != ORES_IOT_HEADER_BYTES + payload_len) return ORES_IOT_MALFORMED;
    out->kind = (ores_iot_kind)frame[5];
    out->payload = frame + ORES_IOT_HEADER_BYTES;
    out->length = payload_len;
    return ORES_IOT_OK;
}
