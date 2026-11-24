#include "ores_iot_frame.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdint.h>
#include <string.h>
int main(void) {
    uint8_t header[ORES_IOT_HEADER_BYTES] = {'O','R','E','S',1,2,0,2};
    ores_iot_kind kind = (ores_iot_kind)99;
    size_t bytes = 99u;
    uint8_t before[sizeof(header)];
    assert(ores_iot_probe_header(header,sizeof header,&kind,&bytes) == ORES_IOT_OK);
    assert(kind == ORES_IOT_COMMAND && bytes == 10u);
    assert(ores_iot_probe_header(header,7u,&kind,&bytes) == ORES_IOT_MALFORMED);
    assert(kind == 0 && bytes == 0u);
    assert(ores_iot_probe_header(NULL,8u,&kind,&bytes) == ORES_IOT_INVALID_ARGUMENT);
    assert(kind == 0 && bytes == 0u);
    assert(ores_iot_probe_header(header,sizeof header,NULL,&bytes) == ORES_IOT_INVALID_ARGUMENT);
    assert(ores_iot_probe_header(header,sizeof header,&kind,NULL) == ORES_IOT_INVALID_ARGUMENT);
    memcpy(before,header,sizeof header);
    assert(ores_iot_probe_header(header,sizeof header,(ores_iot_kind *)(void *)header,&bytes)
           == ORES_IOT_INVALID_ARGUMENT);
    assert(memcmp(before,header,sizeof header)==0);
    assert(ores_iot_probe_header(header,sizeof header,&kind,(size_t *)(void *)header)
           == ORES_IOT_INVALID_ARGUMENT);
    assert(memcmp(before,header,sizeof header)==0);
    header[0]='X';
    assert(ores_iot_probe_header(header,sizeof header,&kind,&bytes) == ORES_IOT_MALFORMED);
    header[0]='O'; header[4]=2;
    assert(ores_iot_probe_header(header,sizeof header,&kind,&bytes) == ORES_IOT_UNSUPPORTED_VERSION);
    header[4]=1; header[5]=255;
    assert(ores_iot_probe_header(header,sizeof header,&kind,&bytes) == ORES_IOT_UNSUPPORTED_KIND);
    header[5]=1; header[6]=4; header[7]=1;
    assert(ores_iot_probe_header(header,sizeof header,&kind,&bytes) == ORES_IOT_TOO_LARGE);
    header[6]=4; header[7]=0;
    assert(ores_iot_probe_header(header,sizeof header,&kind,&bytes) == ORES_IOT_OK);
    assert(kind == ORES_IOT_TELEMETRY && bytes == ORES_IOT_MAX_FRAME_BYTES);
    header[6]=0; header[7]=0;
    assert(ores_iot_probe_header(header,sizeof header,&kind,&bytes) == ORES_IOT_OK);
    assert(bytes == ORES_IOT_HEADER_BYTES);
    return 0;
}
