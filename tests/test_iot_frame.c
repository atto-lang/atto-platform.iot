#include "ores_iot_frame.h"
#ifdef NDEBUG
#undef NDEBUG /* Release builds must execute test assertions */
#endif
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
    {
        static uint8_t oversized[ORES_IOT_MAX_PAYLOAD_BYTES + 1u];
        memset(frame, 0xa5, sizeof frame);
        written = 777;
        assert(ores_iot_encode(ORES_IOT_COMMAND, oversized, sizeof oversized,
                               frame, sizeof frame, &written) == ORES_IOT_TOO_LARGE);
        assert(written == 0 && frame[0] == 0xa5);
        written = 777;
        assert(ores_iot_encode((ores_iot_kind)99, data, sizeof data,
                               frame, sizeof frame, &written) == ORES_IOT_UNSUPPORTED_KIND);
        assert(written == 0 && frame[0] == 0xa5);
        written = 777;
        assert(ores_iot_encode(ORES_IOT_ACK, NULL, 1, frame, sizeof frame, &written) == ORES_IOT_INVALID_ARGUMENT);
        assert(written == 0 && frame[0] == 0xa5);
        written = 777;
        assert(ores_iot_encode(ORES_IOT_ACK, data, sizeof data, NULL, 0, &written) == ORES_IOT_INVALID_ARGUMENT);
        assert(written == 0);
    }
    /* Reject output metadata aliases before touching payload or destination bytes. */
    {
        union { size_t written; ores_iot_view view; uint8_t bytes[64]; } alias;
        uint8_t before[sizeof alias.bytes];
        memset(alias.bytes, 0x5a, sizeof alias.bytes);
        memcpy(before, alias.bytes, sizeof before);
        assert(ores_iot_encode(ORES_IOT_TELEMETRY, alias.bytes, 3,
                               frame, sizeof frame, &alias.written) == ORES_IOT_INVALID_ARGUMENT);
        assert(memcmp(before, alias.bytes, sizeof before) == 0);
        assert(ores_iot_encode(ORES_IOT_TELEMETRY, data, 3,
                               alias.bytes, sizeof alias.bytes, &alias.written) == ORES_IOT_INVALID_ARGUMENT);
        assert(memcmp(before, alias.bytes, sizeof before) == 0);
        assert(ores_iot_encode(ORES_IOT_TELEMETRY, data, 3,
                               frame, sizeof frame, &written) == ORES_IOT_OK);
        memcpy(alias.bytes, frame, written);
        memcpy(before, alias.bytes, sizeof before);
        assert(ores_iot_decode(alias.bytes, written, &alias.view) == ORES_IOT_INVALID_ARGUMENT);
        assert(memcmp(before, alias.bytes, sizeof before) == 0);
    }
    /* Encoding must handle a payload sharing storage with the destination. */
    {
        uint8_t overlap[16] = {0x10u, 0x20u, 0x30u};
        assert(ores_iot_encode(ORES_IOT_TELEMETRY, overlap, 3,
                               overlap, sizeof overlap, &written) == ORES_IOT_OK);
        assert(ores_iot_decode(overlap, written, &parsed) == ORES_IOT_OK);
        assert(parsed.length == 3 && parsed.payload[0] == 0x10 &&
               parsed.payload[1] == 0x20 && parsed.payload[2] == 0x30);
    }
    return 0;
}

