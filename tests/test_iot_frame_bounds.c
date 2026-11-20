#include "ores_iot_frame.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdint.h>
#include <string.h>

/* Full-size boundary oracle: framing is not authentication or OTA approval. */
int main(void) {
    uint8_t payload[ORES_IOT_MAX_PAYLOAD_BYTES];
    uint8_t frame[ORES_IOT_MAX_FRAME_BYTES + 1u];
    ores_iot_view view = {(ores_iot_kind)99, payload, 7};
    size_t written = 99u;
    for (size_t i = 0; i < sizeof payload; ++i) payload[i] = (uint8_t)(i ^ (i >> 8));

    memset(frame, 0xa5, sizeof frame);
    assert(ores_iot_encode(ORES_IOT_COMMAND, payload, sizeof payload,
                           frame, ORES_IOT_MAX_FRAME_BYTES - 1u, &written)
           == ORES_IOT_BUFFER_TOO_SMALL);
    assert(written == 0);
    for (size_t i = 0; i < sizeof frame; ++i) assert(frame[i] == 0xa5);

    assert(ores_iot_encode(ORES_IOT_COMMAND, payload, sizeof payload,
                           frame, ORES_IOT_MAX_FRAME_BYTES, &written) == ORES_IOT_OK);
    assert(written == ORES_IOT_MAX_FRAME_BYTES);
    assert(frame[6] == (uint8_t)(ORES_IOT_MAX_PAYLOAD_BYTES >> 8));
    assert(frame[7] == (uint8_t)(ORES_IOT_MAX_PAYLOAD_BYTES & 255u));
    assert(ores_iot_decode(frame, written, &view) == ORES_IOT_OK);
    assert(view.kind == ORES_IOT_COMMAND);
    assert(view.length == sizeof payload);
    assert(memcmp(view.payload, payload, sizeof payload) == 0);

    assert(ores_iot_decode(frame, written - 1u, &view) == ORES_IOT_MALFORMED);
    assert(view.payload == NULL && view.length == 0);
    assert(ores_iot_decode(frame, written + 1u, &view) == ORES_IOT_MALFORMED);
    assert(view.payload == NULL && view.length == 0);

    frame[6] = (uint8_t)((ORES_IOT_MAX_PAYLOAD_BYTES + 1u) >> 8);
    frame[7] = (uint8_t)((ORES_IOT_MAX_PAYLOAD_BYTES + 1u) & 255u);
    assert(ores_iot_decode(frame, written, &view) == ORES_IOT_TOO_LARGE);
    assert(view.payload == NULL && view.length == 0);
    frame[6] = (uint8_t)(ORES_IOT_MAX_PAYLOAD_BYTES >> 8);
    frame[7] = (uint8_t)(ORES_IOT_MAX_PAYLOAD_BYTES & 255u);

    /* The frame may reuse the payload's buffer; memmove preserves all bytes. */
    memcpy(frame, payload, sizeof payload);
    assert(ores_iot_encode(ORES_IOT_TELEMETRY, frame, sizeof payload,
                           frame, ORES_IOT_MAX_FRAME_BYTES, &written) == ORES_IOT_OK);
    assert(ores_iot_decode(frame, written, &view) == ORES_IOT_OK);
    assert(view.kind == ORES_IOT_TELEMETRY);
    assert(view.length == sizeof payload);
    assert(memcmp(view.payload, payload, sizeof payload) == 0);
    return 0;
}
