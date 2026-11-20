/* Host-only wire frame parser rejection/recovery contract.
 * cc -std=c11 -Wall -Wextra -Wpedantic -Werror -Iinclude \
 *    src/ores_iot_frame.c tests/test_gpt6_wave242_frame_failclosed_20261106.c \
 *    -o /tmp/ores-iot-wave242 && /tmp/ores-iot-wave242
 * Not authenticated transport, an actor guest, or hardware evidence.
 */
#include "ores_iot_frame.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static void stale(ores_iot_view *view) {
    static const uint8_t sentinel = 99;
    view->kind = ORES_IOT_COMMAND;
    view->payload = &sentinel;
    view->length = 123u;
}
static void denied(ores_iot_status expected, const uint8_t *wire,
                   size_t length, ores_iot_view *view) {
    stale(view);
    assert(ores_iot_decode(wire, length, view) == expected);
    assert((int)view->kind == 0 && view->payload == NULL && view->length == 0u);
}
int main(void) {
    const uint8_t message[] = {0u, 0xffu, 0x7eu};
    uint8_t wire[ORES_IOT_HEADER_BYTES + sizeof message + 1u] = {0};
    size_t written = 99u;
    ores_iot_view view;
    assert(ores_iot_encode(ORES_IOT_ACK, message, sizeof message,
                           wire, sizeof wire, &written) == ORES_IOT_OK);
    assert(written == ORES_IOT_HEADER_BYTES + sizeof message);
    assert(ores_iot_decode(wire, written, &view) == ORES_IOT_OK);
    assert(view.kind == ORES_IOT_ACK && view.length == sizeof message);
    assert(view.payload == wire + ORES_IOT_HEADER_BYTES);
    assert(view.payload[0] == 0u && view.payload[1] == 0xffu &&
           view.payload[2] == 0x7eu);

    wire[0] = 'X';
    denied(ORES_IOT_MALFORMED, wire, written, &view);
    wire[0] = 'O';
    wire[4] = 99u;
    denied(ORES_IOT_UNSUPPORTED_VERSION, wire, written, &view);
    wire[4] = ORES_IOT_FRAME_VERSION;
    wire[5] = 99u;
    denied(ORES_IOT_UNSUPPORTED_KIND, wire, written, &view);
    wire[5] = ORES_IOT_ACK;
    denied(ORES_IOT_MALFORMED, wire, written - 1u, &view);
    denied(ORES_IOT_MALFORMED, wire, written + 1u, &view);
    wire[6] = 0x04u;
    wire[7] = 0x01u;
    denied(ORES_IOT_TOO_LARGE, wire, written, &view);
    wire[6] = 0u;
    wire[7] = (uint8_t)sizeof message;
    assert(ores_iot_decode(wire, written, &view) == ORES_IOT_OK);
    assert(view.kind == ORES_IOT_ACK && view.length == sizeof message);
    puts("IoT frame decoder fail-closed recovery: PASS");
    return 0;
}
