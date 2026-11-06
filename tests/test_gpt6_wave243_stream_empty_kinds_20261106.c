/* Host C stream acceptance of zero-length telemetry, command and ACK frames.
 * Compile with src/ores_iot_frame.c and src/ores_iot_stream.c under strict C11.
 * Not an authenticated transport or compiled AttoLang guest.
 */
#include "ores_iot_stream.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr,"iot wave243: %s:%d %s\n",__FILE__,__LINE__,#x); exit(1); } } while (0)

typedef struct { unsigned seen; unsigned sequence[3]; } receipt;
static ores_iot_status accept(void *ctx, const ores_iot_view *view) {
    receipt *r = ctx;
    CHECK(view->length == 0);
    CHECK(r->seen < 3);
    CHECK(view->kind == (ores_iot_kind)r->sequence[r->seen]);
    ++r->seen;
    return ORES_IOT_OK;
}
static void assert_empty(const ores_iot_stream *s) {
    size_t i;
    for (i=0; i<sizeof s->buffer; ++i) CHECK(s->buffer[i]==0);
}
int main(void) {
    const ores_iot_kind kinds[] = {ORES_IOT_TELEMETRY, ORES_IOT_COMMAND, ORES_IOT_ACK};
    uint8_t frames[3 * ORES_IOT_HEADER_BYTES] = {0};
    ores_iot_stream stream;
    receipt r = {0, {ORES_IOT_TELEMETRY, ORES_IOT_COMMAND, ORES_IOT_ACK}};
    size_t i, n, delivered;

    for (i=0; i<3; ++i) {
        n=99;
        CHECK(ores_iot_encode(kinds[i], NULL, 0, frames + i*ORES_IOT_HEADER_BYTES,
                              ORES_IOT_HEADER_BYTES, &n) == ORES_IOT_OK);
        CHECK(n == ORES_IOT_HEADER_BYTES);
    }
    CHECK(ores_iot_stream_init(&stream) == ORES_IOT_OK);
    for (i=0; i<sizeof frames; ++i) {
        delivered=99;
        CHECK(ores_iot_stream_feed(&stream, frames+i, 1, accept, &r, &delivered)
              == ORES_IOT_OK);
        CHECK(delivered == ((i+1u)%ORES_IOT_HEADER_BYTES==0 ? 1u : 0u));
        CHECK(r.seen == (unsigned)((i+1u)/ORES_IOT_HEADER_BYTES));
        if (delivered) {
            CHECK(stream.used==0 && stream.expected==0);
            assert_empty(&stream);
        }
    }
    CHECK(r.seen == 3);
    CHECK(ores_iot_stream_finish(&stream) == ORES_IOT_OK);
    assert_empty(&stream);
    CHECK(ores_iot_stream_finish(&stream) == ORES_IOT_CLOSED);

    /* Reopening must clear closed state and deliver all independent frames in
     * one bounded feed, while retaining the exact callback sequence. */
    r.seen=0;
    CHECK(ores_iot_stream_init(&stream) == ORES_IOT_OK);
    delivered=99;
    CHECK(ores_iot_stream_feed(&stream, frames, sizeof frames, accept, &r, &delivered)
          == ORES_IOT_OK);
    CHECK(delivered==3 && r.seen==3 && stream.used==0);
    assert_empty(&stream);
    CHECK(ores_iot_stream_finish(&stream) == ORES_IOT_OK);
    puts("iot empty-kind stream fragmentation and restart: PASS");
    return 0;
}
