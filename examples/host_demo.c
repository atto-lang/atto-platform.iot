#include "ores_iot_frame.h"
#include <stdint.h>
#include <stdio.h>
int main(void) {
    const uint8_t telemetry[] = {0x42u, 0x17u};
    uint8_t wire[ORES_IOT_HEADER_BYTES + sizeof telemetry];
    size_t written = 0;
    ores_iot_view decoded;
    if (ores_iot_encode(ORES_IOT_TELEMETRY, telemetry, sizeof telemetry,
                        wire, sizeof wire, &written) != ORES_IOT_OK) return 1;
    if (ores_iot_decode(wire, written, &decoded) != ORES_IOT_OK) return 1;
    printf("OresIoT frame v%u: kind=%u, payload=%zu bytes\n",
           ORES_IOT_FRAME_VERSION, (unsigned)decoded.kind, decoded.length);
    return decoded.length == sizeof telemetry && decoded.payload[0] == telemetry[0] ? 0 : 1;
}
