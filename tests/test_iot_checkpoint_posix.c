#define _POSIX_C_SOURCE 200809L
#include "ores_iot_checkpoint_posix.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define PEER UINT64_C(0x1234567890abcdef)
#define EPOCH UINT64_C(7)
static int provision(ores_iot_checkpoint_io *io) {
    ores_iot_replay_window w={10,1};
    uint8_t a[64],b[64],marker[8];
    if(!io->read_slot(io->context,0,a) || !io->read_slot(io->context,1,b)) return 0;
    if(ores_iot_checkpoint_prepare(&w,PEER,EPOCH,1,a)!=ORES_IOT_CHECKPOINT_OK ||
       ores_iot_checkpoint_marker(marker)!=ORES_IOT_CHECKPOINT_OK) return 0;
    return io->write_bytes(io->context,0,56,a+56,8) && io->flush(io->context,0) &&
           io->write_bytes(io->context,0,0,a,56) && io->flush(io->context,0) &&
           io->publish_marker(io->context,0,marker);
}
static void check(ores_iot_checkpoint_io *io,uint64_t generation,uint64_t highest) {
    ores_iot_checkpoint_value val={0,{0,0}};
    assert(ores_iot_checkpoint_load(io,PEER,EPOCH,&val)==ORES_IOT_STORE_OK);
    assert(val.generation==generation && val.window.highest==highest);
}
/* Model an abrupt process death at a specified storage callback boundary. */
typedef struct {ores_iot_checkpoint_io underlying;unsigned ops,cut,after_publish;} killer;
static int kill_write(void *ctx,unsigned slot,size_t off,const uint8_t *p,size_t n) {
    killer *k=ctx;
    if(++k->ops==k->cut) _exit(74);
    return k->underlying.write_bytes(k->underlying.context,slot,off,p,n);
}
static int kill_flush(void *ctx,unsigned slot) {
    killer *k=ctx;
    if(++k->ops==k->cut) _exit(74);
    return k->underlying.flush(k->underlying.context,slot);
}
static int kill_publish(void *ctx,unsigned slot,const uint8_t marker[8]) {
    killer *k=ctx;
    if(++k->ops==k->cut) _exit(74);
    {int result=k->underlying.publish_marker(k->underlying.context,slot,marker);
     if(k->after_publish) _exit(75);
     return result;
    }
}
static int kill_read(void *ctx,unsigned slot,uint8_t out[64]) {
    killer *k=ctx;
    return k->underlying.read_slot(k->underlying.context,slot,out);
}
static void test_process_crashes(const char *dir) {
    unsigned cut;
    for(cut=1;cut<=5;++cut) {
        pid_t child;
        int status;
        ores_iot_checkpoint_posix store;
        ores_iot_checkpoint_io io;
        /* Provision before fork, then RELEASE the exclusive writer lease. */
        if(cut==1) {
            assert(ores_iot_checkpoint_posix_open(&store,dir));
            io=ores_iot_checkpoint_posix_adapter(&store);
            assert(provision(&io));
            ores_iot_checkpoint_posix_close(&store);
        }
        child=fork();assert(child>=0);
        if(child==0) {
            killer k;
            assert(ores_iot_checkpoint_posix_open(&store,dir));
            io=ores_iot_checkpoint_posix_adapter(&store);
            ores_iot_checkpoint_io wrapped;
            ores_iot_checkpoint_value result;
            k.underlying=io;k.ops=0;k.cut=cut;k.after_publish=0;
            wrapped.context=&k;wrapped.read_slot=kill_read;
            wrapped.write_bytes=kill_write;wrapped.flush=kill_flush;
            wrapped.publish_marker=kill_publish;
            (void)ores_iot_checkpoint_commit_sequence(&wrapped,PEER,EPOCH,
                              cut,10+cut,&result);
            _exit(73);
        }
        assert(waitpid(child,&status,0)==child);
        assert(WIFEXITED(status) && WEXITSTATUS(status)==74);
        assert(ores_iot_checkpoint_posix_open(&store,dir));
        io=ores_iot_checkpoint_posix_adapter(&store);
        check(&io,cut,9+cut);
        {ores_iot_checkpoint_value result;
         assert(ores_iot_checkpoint_commit_sequence(&io,PEER,EPOCH,cut,10+cut,&result)
                ==ORES_IOT_STORE_OK);
         assert(result.generation==cut+1);
        }
        ores_iot_checkpoint_posix_close(&store);
    }
    /* Crash after a completed fsync+rename, but before returning success. */
    {
        pid_t child=fork();int status;
        assert(child>=0);
        if(child==0) {
            ores_iot_checkpoint_posix store;
            ores_iot_checkpoint_io io,wrapped;
            ores_iot_checkpoint_value result;
            killer k;
            assert(ores_iot_checkpoint_posix_open(&store,dir));
            io=ores_iot_checkpoint_posix_adapter(&store);
            k.underlying=io;k.ops=0;k.cut=0;k.after_publish=1;
            wrapped.context=&k;wrapped.read_slot=kill_read;
            wrapped.write_bytes=kill_write;wrapped.flush=kill_flush;
            wrapped.publish_marker=kill_publish;
            (void)ores_iot_checkpoint_commit_sequence(&wrapped,PEER,EPOCH,6,16,&result);
            _exit(73);
        }
        assert(waitpid(child,&status,0)==child);
        assert(WIFEXITED(status) && WEXITSTATUS(status)==75);
        ores_iot_checkpoint_posix store;
        ores_iot_checkpoint_io io;
        ores_iot_checkpoint_value result;
        assert(ores_iot_checkpoint_posix_open(&store,dir));
        io=ores_iot_checkpoint_posix_adapter(&store);
        check(&io,7,16);
        assert(ores_iot_checkpoint_commit_sequence(&io,PEER,EPOCH,7,16,&result)==
            ORES_IOT_STORE_REPLAY_DUPLICATE);
        ores_iot_checkpoint_posix_close(&store);
    }
}
int main(void) {
    char pattern[]="/tmp/ores-iot-posix-test-XXXXXX";
    char file[512];
    ores_iot_checkpoint_posix store;
    ores_iot_checkpoint_io io;
    ores_iot_checkpoint_value val;
    int fd;
    assert(mkdtemp(pattern)!=NULL);
    assert(chmod(pattern,0777)==0);
    assert(!ores_iot_checkpoint_posix_open(&store,pattern));
    assert(chmod(pattern,0700)==0);
    assert(ores_iot_checkpoint_posix_open(&store,pattern));
    {ores_iot_checkpoint_posix other;
     assert(!ores_iot_checkpoint_posix_open(&other,pattern));
    }
    io=ores_iot_checkpoint_posix_adapter(&store);
    assert(ores_iot_checkpoint_load(&io,PEER,EPOCH,&val)==ORES_IOT_STORE_EMPTY);
    ores_iot_checkpoint_posix_close(&store);
    {
        size_t i;
        const uint8_t *raw=(const uint8_t *)store.staged;
        for(i=0;i<sizeof(store.staged);++i) assert(raw[i]==0);
    }
    test_process_crashes(pattern);
    assert(ores_iot_checkpoint_posix_open(&store,pattern));
    io=ores_iot_checkpoint_posix_adapter(&store);
    check(&io,7,16);
    ores_iot_checkpoint_posix_close(&store);
    snprintf(file,sizeof file,"%s/slot0.ockp",pattern);
    fd=open(file,O_RDWR);assert(fd>=0);
    /* Never downgrade to another published slot after corruption. */
    assert(pwrite(fd,"Z",1,30)==1);
    assert(fsync(fd)==0 && close(fd)==0);
    assert(ores_iot_checkpoint_posix_open(&store,pattern));
    io=ores_iot_checkpoint_posix_adapter(&store);
    assert(ores_iot_checkpoint_load(&io,PEER,EPOCH,&val)==ORES_IOT_STORE_UNTRUSTED);
    ores_iot_checkpoint_posix_close(&store);
    snprintf(file,sizeof file,"%s/slot0.ockp",pattern);
    assert(unlink(file)==0);
    /* Existing symlinks and truncated slot files fail closed. */
    assert(symlink("/etc/passwd",file)==0);
    assert(ores_iot_checkpoint_posix_open(&store,pattern));
    io=ores_iot_checkpoint_posix_adapter(&store);
    assert(ores_iot_checkpoint_load(&io,PEER,EPOCH,&val)==ORES_IOT_STORE_IO_ERROR);
    ores_iot_checkpoint_posix_close(&store);
    assert(unlink(file)==0);
    fd=open(file,O_CREAT|O_EXCL|O_WRONLY,0600);assert(fd>=0);
    assert(write(fd,"X",1)==1);
    assert(close(fd)==0);
    assert(ores_iot_checkpoint_posix_open(&store,pattern));
    io=ores_iot_checkpoint_posix_adapter(&store);
    assert(ores_iot_checkpoint_load(&io,PEER,EPOCH,&val)==ORES_IOT_STORE_IO_ERROR);
    ores_iot_checkpoint_posix_close(&store);
    assert(unlink(file)==0);
    snprintf(file,sizeof file,"%s/slot1.ockp",pattern);
    assert(unlink(file)==0);
    assert(rmdir(pattern)==0);
    puts("IoT POSIX rename/fsync host process-crash test: PASS");
    return 0;
}
