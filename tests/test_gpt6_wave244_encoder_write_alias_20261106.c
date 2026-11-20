/* Host-only framing admission: written-count aliasing and input/output overlap.
 * cc -std=c11 -Wall -Wextra -Wpedantic -Werror -Iinclude src/ores_iot_frame.c \
 *   tests/test_gpt6_wave244_encoder_write_alias_20261106.c -o /tmp/ores-wave244-iot && /tmp/ores-wave244-iot
 * These tests do not authenticate remote messages or certify embedded hardware.
 */
#include "ores_iot_frame.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    union { size_t count; uint8_t bytes[32]; } shared;
    uint8_t output[32];
    uint8_t before[32];
    size_t written = 99u;
    uint8_t overlap[16] = {0, 7, 8, 9, 0};

    memset(shared.bytes, 0xa5, sizeof shared.bytes);
    memcpy(before, shared.bytes, sizeof before);
    assert(ores_iot_encode(ORES_IOT_ACK, NULL, 0u,
                           shared.bytes, sizeof shared.bytes, &shared.count)
           == ORES_IOT_INVALID_ARGUMENT);
    assert(memcmp(shared.bytes, before, sizeof before) == 0);

    memset(output, 0x5a, sizeof output);
    memcpy(before, output, sizeof before);
    assert(ores_iot_encode(ORES_IOT_TELEMETRY, shared.bytes, 3u,
                           output, sizeof output, &shared.count)
           == ORES_IOT_INVALID_ARGUMENT);
    assert(memcmp(output, before, sizeof before) == 0);

    assert(ores_iot_encode(ORES_IOT_COMMAND, NULL, 1u,
                           output, sizeof output, &written)
           == ORES_IOT_INVALID_ARGUMENT);
    assert(written == 0u && memcmp(output, before, sizeof before) == 0);

    assert(ores_iot_encode(ORES_IOT_COMMAND, overlap + 1, 3u,
                           overlap, sizeof overlap, &written) == ORES_IOT_OK);
    assert(written == 11u);
    assert(overlap[8] == 7u && overlap[9] == 8u && overlap[10] == 9u);
    ores_iot_view view = {(ores_iot_kind)0, NULL, 0u};
    assert(ores_iot_decode(overlap, written, &view) == ORES_IOT_OK);
    assert(view.kind == ORES_IOT_COMMAND && view.length == 3u);
    assert(view.payload[0] == 7u && view.payload[1] == 8u && view.payload[2] == 9u);

    puts("IoT encoder alias/no-write and overlapping payload: PASS");
    return 0;
}
