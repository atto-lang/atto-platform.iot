/* Standalone libFuzzer entrypoint, build with clang -fsanitize=fuzzer,address. */
#include "ores_iot_frame.h"
#include <stddef.h>
#include <stdint.h>
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    ores_iot_view view;
    (void)ores_iot_decode(data, size, &view);
    return 0;
}
