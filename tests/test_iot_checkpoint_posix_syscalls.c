#define _POSIX_C_SOURCE 200809L
#include "ores_iot_checkpoint_posix.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

typedef enum { NO_FAULT, FAIL_EINTR, FAIL_EIO, FAIL_ENOSPC,
               FAIL_ZERO, SHORT_IO, FAIL_AFTER_SYSCALL } failure;
typedef enum { HOOK_NONE, HOOK_OPENAT, HOOK_READ, HOOK_WRITE,
               HOOK_FSYNC, HOOK_RENAMEAT, HOOK_CLOSE } operation;
static operation injected;
static failure behavior;
static unsigned calls, ordinal;
static unsigned attempts;
static void inject(operation op, failure type, unsigned nth) {
    injected=op;behavior=type;ordinal=nth;calls=0;attempts=0;
}
static int hit(operation op) {
    if(injected!=op) return 0;
    ++calls;
    if(calls!=ordinal) return 0;
    ++attempts;
    return 1;
}
static int wrap_openat(int fd,const char *name,int flags,...) {
    if(hit(HOOK_OPENAT)) { errno=ENOSPC;return -1; }
    if(flags&O_CREAT) return openat(fd,name,flags,0600);
    return openat(fd,name,flags);
}
static ssize_t wrap_read(int fd,void *buf,size_t len) {
    if(hit(HOOK_READ)) {
        if(behavior==FAIL_EINTR){errno=EINTR;return -1;}
        if(behavior==SHORT_IO) return read(fd,buf,1);
        errno=EIO;return -1;
    }
    return read(fd,buf,len);
}
static ssize_t wrap_write(int fd,const void *buf,size_t len) {
    if(hit(HOOK_WRITE)) {
        if(behavior==FAIL_EINTR){errno=EINTR;return -1;}
        if(behavior==SHORT_IO) return write(fd,buf,1);
        if(behavior==FAIL_ZERO) return 0;
        errno=(behavior==FAIL_ENOSPC?ENOSPC:EIO);return -1;
    }
    return write(fd,buf,len);
}
static int wrap_fsync(int fd) {
    if(hit(HOOK_FSYNC)) {
        if(behavior==FAIL_AFTER_SYSCALL){assert(fsync(fd)==0);errno=EIO;return -1;}
        errno=(behavior==FAIL_EINTR?EINTR:EIO);return -1;
    }
    return fsync(fd);
}
static int wrap_renameat(int a,const char *b,int c,const char *d) {
    if(hit(HOOK_RENAMEAT)) {
        if(behavior==FAIL_AFTER_SYSCALL){assert(renameat(a,b,c,d)==0);errno=EIO;return -1;}
        errno=(behavior==FAIL_EINTR?EINTR:EIO);return -1;
    }
    return renameat(a,b,c,d);
}
static int wrap_close(int fd) {
    if(hit(HOOK_CLOSE)) {
        if(behavior==FAIL_AFTER_SYSCALL){assert(close(fd)==0);errno=EIO;return -1;}
        errno=EIO;return -1;
    }
    return close(fd);
}
/* Compile production adapter against narrowly scoped syscall interposition.
 * No production code or POSIX flags are changed by this test-only harness. */
#define openat wrap_openat
#define read wrap_read
#define write wrap_write
#define fsync wrap_fsync
#define renameat wrap_renameat
#define close wrap_close
#include "../src/ores_iot_checkpoint_posix.c"
#undef openat
#undef read
#undef write
#undef fsync
#undef renameat
#undef close
static unsigned tmp_count(const char *dir) {
    DIR *handle=opendir(dir);struct dirent *entry;unsigned n=0;
    assert(handle);
    while((entry=readdir(handle))!=NULL)
        if(strstr(entry->d_name,".tmp")!=NULL)++n;
    assert(closedir(handle)==0);
    return n;
}
static int exists(const char *dir) {
    char filename[512];struct stat st;
    assert(snprintf(filename,sizeof filename,"%s/slot0.ockp",dir)>0);
    return stat(filename,&st)==0 && st.st_size==64;
}
static void exercise(operation op,failure fail,unsigned nth,int expect_success,int expect_visible) {
    char dir[]="/tmp/ores-iot-syscall-XXXXXX",file[512];
    uint8_t original[64]={0},payload[56],received[64]={0};
    ores_iot_checkpoint_posix store;
    ores_iot_checkpoint_io io;
    unsigned i;
    assert(mkdtemp(dir)!=NULL);
    assert(ores_iot_checkpoint_posix_open(&store,dir));
    io=ores_iot_checkpoint_posix_adapter(&store);
    for(i=0;i<56;++i)payload[i]=(uint8_t)(i+1);
    assert(io.read_slot(io.context,0,original));
    assert(io.write_bytes(io.context,0,0,payload,sizeof payload));
    inject(op,fail,nth);
    assert(io.flush(io.context,0)==expect_success);
    assert(attempts==1);
    inject(HOOK_NONE,NO_FAULT,0);
    assert(exists(dir)==expect_visible);
    assert(tmp_count(dir)==0);
    if(expect_visible){
        assert(io.read_slot(io.context,0,received));
        assert(memcmp(received,payload,56)==0);
    }
    ores_iot_checkpoint_posix_close(&store);
    assert(snprintf(file,sizeof file,"%s/slot0.ockp",dir)>0);
    if(expect_visible)assert(unlink(file)==0);
    assert(rmdir(dir)==0);
}
static void read_faults(void) {
    char dir[]="/tmp/ores-iot-read-XXXXXX",file[512];
    ores_iot_checkpoint_posix store;
    ores_iot_checkpoint_io io;
    uint8_t original[64]={0},received[64];
    assert(mkdtemp(dir)!=NULL);
    assert(ores_iot_checkpoint_posix_open(&store,dir));
    io=ores_iot_checkpoint_posix_adapter(&store);
    assert(io.read_slot(io.context,0,original));
    assert(io.flush(io.context,0));
    memset(received,0xa5,sizeof received);
    inject(HOOK_READ,FAIL_EIO,1);
    assert(!io.read_slot(io.context,0,received));
    assert(attempts==1);
    for(size_t i=0;i<64;++i) {
        assert(received[i]==0xa5);
        assert(store.staged[0][i]==0);
    }
    inject(HOOK_NONE,NO_FAULT,0);
    /* Failed reads must invalidate cached state; no stale flush allowed. */
    assert(!io.flush(io.context,0));
    inject(HOOK_READ,FAIL_EINTR,1);
    assert(io.read_slot(io.context,0,received));
    assert(attempts==1);
    inject(HOOK_READ,SHORT_IO,1);
    assert(io.read_slot(io.context,0,received));
    assert(attempts==1);
    inject(HOOK_NONE,NO_FAULT,0);
    ores_iot_checkpoint_posix_close(&store);
    assert(snprintf(file,sizeof file,"%s/slot0.ockp",dir)>0);
    assert(unlink(file)==0);
    assert(rmdir(dir)==0);
}
int main(void) {
    exercise(HOOK_OPENAT,FAIL_ENOSPC,1,0,0);
    exercise(HOOK_WRITE,FAIL_EIO,1,0,0);
    exercise(HOOK_WRITE,FAIL_ENOSPC,1,0,0);
    exercise(HOOK_WRITE,FAIL_ZERO,1,0,0);
    exercise(HOOK_WRITE,FAIL_EINTR,1,1,1);
    exercise(HOOK_WRITE,SHORT_IO,1,1,1);
    exercise(HOOK_FSYNC,FAIL_EIO,1,0,0); /* file fsync */
    exercise(HOOK_FSYNC,FAIL_EINTR,1,1,1);
    exercise(HOOK_RENAMEAT,FAIL_EIO,1,0,0);
    exercise(HOOK_RENAMEAT,FAIL_AFTER_SYSCALL,1,0,1);
    exercise(HOOK_FSYNC,FAIL_EIO,2,0,1); /* directory fsync */
    exercise(HOOK_FSYNC,FAIL_AFTER_SYSCALL,2,0,1);
    exercise(HOOK_CLOSE,FAIL_AFTER_SYSCALL,1,0,0);
    read_faults();
    puts("IoT POSIX syscall fault injection: PASS");
    return 0;
}
