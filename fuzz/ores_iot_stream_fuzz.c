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
    size_t n = 0;
    if (ores_iot_stream_init(&state) != ORES_IOT_OK) return 0;
    (void)ores_iot_stream_feed(&state, bytes, length, consume, NULL, &n);
    return 0;
}
