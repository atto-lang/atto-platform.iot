#include "ores_iot_frame.h"
#include <assert.h>
#include <stdint.h>
#include <string.h>

int main(void) {
    uint8_t frame[ORES_IOT_HEADER_BYTES + ORES_IOT_MAX_PAYLOAD_BYTES + 1u] = {0};
    const uint8_t data[] = {0x10u, 0x20u, 0x30u};
    size_t written = 777;
    ores_iot_view parsed = {(ores_iot_kind)99, data, 3};
    assert(ores_iot_encode(ORES_IOT_TELEMETRY, data, 3, frame, sizeof(frame), &written) == ORES_IOT_OK);
    assert(written == 11);
    assert(ores_iot_decode(frame, written, &parsed) == ORES_IOT_OK);
    assert(parsed.kind == ORES_IOT_TELEMETRY && parsed.length == 3);
    assert(memcmp(parsed.payload, data, 3) == 0);
    assert(ores_iot_decode(frame, written + 1, &parsed) == ORES_IOT_MALFORMED);
    assert(parsed.payload == NULL && parsed.length == 0);
    assert(ores_iot_decode(frame, written - 1, &parsed) == ORES_IOT_MALFORMED);
    frame[4] = 2;
    assert(ores_iot_decode(frame, written, &parsed) == ORES_IOT_UNSUPPORTED_VERSION);
    frame[4] = 1; frame[5] = 99;
    assert(ores_iot_decode(frame, written, &parsed) == ORES_IOT_UNSUPPORTED_KIND);
    frame[5] = ORES_IOT_TELEMETRY; frame[0] = 0;
    assert(ores_iot_decode(frame, written, &parsed) == ORES_IOT_MALFORMED);
    frame[0] = 'O'; frame[6] = 0x08u; frame[7] = 0u;
    assert(ores_iot_decode(frame, written, &parsed) == ORES_IOT_TOO_LARGE);
    frame[6] = 0; frame[7] = 3;
    assert(ores_iot_decode(frame, 3, &parsed) == ORES_IOT_MALFORMED);
    assert(ores_iot_decode(NULL, written, &parsed) == ORES_IOT_INVALID_ARGUMENT);
    assert(ores_iot_decode(frame, written, NULL) == ORES_IOT_INVALID_ARGUMENT);
    assert(ores_iot_encode(ORES_IOT_TELEMETRY, data, 3, frame, 10, &written) == ORES_IOT_BUFFER_TOO_SMALL);
    assert(written == 0);
    assert(ores_iot_encode(ORES_IOT_TELEMETRY, NULL, 3, frame, sizeof frame, &written) == ORES_IOT_INVALID_ARGUMENT);
    assert(ores_iot_encode(ORES_IOT_TELEMETRY, data, 3, frame, sizeof frame, NULL) == ORES_IOT_INVALID_ARGUMENT);
    assert(ores_iot_encode((ores_iot_kind)0, data, 3, frame, sizeof frame, &written) == ORES_IOT_UNSUPPORTED_KIND);
    assert(ores_iot_encode(ORES_IOT_ACK, NULL, 0, frame, sizeof frame, &written) == ORES_IOT_OK);
    assert(written == ORES_IOT_HEADER_BYTES);
    assert(ores_iot_decode(frame, written, &parsed) == ORES_IOT_OK);
    assert(parsed.kind == ORES_IOT_ACK && parsed.length == 0);
    assert(ores_iot_encode(ORES_IOT_COMMAND, data, ORES_IOT_MAX_PAYLOAD_BYTES + 1u,
                           frame, sizeof frame, &written) == ORES_IOT_TOO_LARGE);
    return 0;
}
