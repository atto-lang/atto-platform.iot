/* Optional libFuzzer entrypoint; build separately with clang -fsanitize=fuzzer,address. */
#include "ores_iot_stream.h"
#include <stddef.h>
#include <stdint.h>
static ores_iot_status consume(void *ctx, const ores_iot_view *view) {
    (void)ctx; (void)view;
    return ORES_IOT_OK;
}
int LLVMFuzzerTestOneInput(const uint8_t *bytes, size_t length) {
    ores_iot_stream state;
    size_t offset = 0, n = 0;
    ores_iot_status status = ORES_IOT_OK;
    if (ores_iot_stream_init(&state) != ORES_IOT_OK) return 0;
    while (offset < length && status == ORES_IOT_OK) {
        size_t part = length - offset;
        if (part > ORES_IOT_MAX_FEED_BYTES) part = ORES_IOT_MAX_FEED_BYTES;
        status = ores_iot_stream_feed(&state, bytes + offset, part, consume, NULL, &n);
        offset += part;
    }
    if (status == ORES_IOT_OK) (void)ores_iot_stream_finish(&state);
    return 0;
}
