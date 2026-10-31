#define _POSIX_C_SOURCE 200809L
#include "ores_iot_checkpoint_posix.h"
#include "ores_iot_posix_orphans_internal.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/* Security-sensitive flags must never silently degrade to zero. */
#if !defined(O_CLOEXEC) || !defined(O_NOFOLLOW) || !defined(O_DIRECTORY) || !defined(O_NONBLOCK)
#error "Linux checkpoint adapter requires O_CLOEXEC, O_NOFOLLOW, O_DIRECTORY, O_NONBLOCK"
#endif

static const char *const slot_names[2] = {"slot0.ockp", "slot1.ockp"};
static int safe_fd(int fd) {
    struct stat st;
    return fd >= 0 && fstat(fd, &st) == 0 && S_ISDIR(st.st_mode) &&
        st.st_uid == geteuid() && (st.st_mode & 0077) == 0;
}
int ores_iot_checkpoint_posix_open(ores_iot_checkpoint_posix *store,
                                   const char *trusted_directory) {
    int fd;
    if (!store || !trusted_directory || !trusted_directory[0]) return 0;
    fd = open(trusted_directory, O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);
    if (!safe_fd(fd) || flock(fd, LOCK_EX|LOCK_NB)!=0 ||
        !ores_iot_cleanup_orphan_temps(fd)) {
        if (fd >= 0) close(fd);
        return 0;
    }
    memset(store, 0, sizeof(*store));
    store->dirfd=fd;
    store->owner_pid=(long)getpid();
    return 1;
}
void ores_iot_checkpoint_posix_close(ores_iot_checkpoint_posix *store) {
    if (!store) return;
    size_t i;
    volatile unsigned char *s=(volatile unsigned char *)store->staged;
    if (store->dirfd >= 0) close(store->dirfd);
    store->dirfd=-1;
    store->owner_pid=0;
    store->serial=0;
    for(i=0;i<sizeof(store->staged);++i) s[i]=0;
    memset(store->loaded,0,sizeof store->loaded);
}
static void scrub_bytes(volatile unsigned char *p, size_t n) {
    size_t i;
    for (i=0; i<n; ++i) p[i]=0;
}
static int read_slot(void *context, unsigned slot,
                     uint8_t out[ORES_IOT_CHECKPOINT_BYTES]) {
    ores_iot_checkpoint_posix *store=context;
    struct stat st;
    uint8_t snapshot[ORES_IOT_CHECKPOINT_BYTES]={0};
    int fd;
    size_t n=0;
    if (!store || store->dirfd < 0 || store->owner_pid!=(long)getpid() ||
        slot > 1 || !out) return 0;
    /* Every new read revokes the previous staged snapshot. A failed read
     * cannot leave stale bytes eligible for flush or marker publication. */
    store->loaded[slot]=0;
    scrub_bytes(store->staged[slot],sizeof store->staged[slot]);
    /* A same-UID attacker or corrupt slot may be a FIFO. O_NOFOLLOW alone
     * does not prevent a blocking open before fstat can reject non-files. */
    fd=openat(store->dirfd, slot_names[slot], O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK);
    if (fd < 0) {
        if (errno != ENOENT) return 0;
    } else {
        int ok=fstat(fd,&st)==0 && S_ISREG(st.st_mode) &&
               st.st_size==(off_t)ORES_IOT_CHECKPOINT_BYTES &&
               st.st_uid==geteuid() && (st.st_mode & 0777)==0600 &&
               st.st_nlink == 1;
        while(ok && n<ORES_IOT_CHECKPOINT_BYTES) {
            ssize_t got=read(fd,snapshot+n,ORES_IOT_CHECKPOINT_BYTES-n);
            if (got<0 && errno==EINTR) continue;
            if (got<=0) { ok=0; break; }
            n+=(size_t)got;
        }
        if(close(fd)!=0) ok=0;
        if(!ok) {
            scrub_bytes(snapshot,sizeof snapshot);
            return 0;
        }
    }
    /* Publish output and cache only after an entire successful read.
     * A partial read or failed close leaves the caller's out unchanged. */
    memcpy(store->staged[slot],snapshot,sizeof snapshot);
    memcpy(out,snapshot,sizeof snapshot);
    scrub_bytes(snapshot,sizeof snapshot);
    store->loaded[slot]=1;
    return 1;
}
static int write_bytes(void *context,unsigned slot,size_t offset,
                       const uint8_t *bytes,size_t length) {
    ores_iot_checkpoint_posix *store=context;
    static const uint8_t unpublished[ORES_IOT_CHECKPOINT_MARKER_BYTES]={0};
    if (!store || store->dirfd<0 || store->owner_pid!=(long)getpid() ||
        slot>1 || !store->loaded[slot] ||
        !bytes || offset>ORES_IOT_CHECKPOINT_BYTES ||
        length>ORES_IOT_CHECKPOINT_BYTES-offset) return 0;
    /* The final publication marker has a single writer: publish_marker.
     * Ordinary staged writes must not be able to forge a published record
     * or cross the marker boundary. Allow only a complete zero invalidation
     * of that field, as required by the ordered commit protocol. */
    if (offset>=ORES_IOT_CHECKPOINT_MARKER_OFFSET) {
        if (offset!=ORES_IOT_CHECKPOINT_MARKER_OFFSET ||
            length!=ORES_IOT_CHECKPOINT_MARKER_BYTES ||
            memcmp(bytes,unpublished,sizeof unpublished)!=0) return 0;
    } else if (length>ORES_IOT_CHECKPOINT_MARKER_OFFSET-offset) return 0;
    memcpy(store->staged[slot]+offset,bytes,length);
    return 1;
}
static int write_all(int fd,const uint8_t *p,size_t n) {
    size_t written=0;
    while(written<n) {
        ssize_t count=write(fd,p+written,n-written);
        if(count<0 && errno==EINTR) continue;
        if(count<=0) return 0;
        written+=(size_t)count;
    }
    return 1;
}
static int sync_fd(int fd) {
    int r;
    do { r=fsync(fd); } while(r<0 && errno==EINTR);
    return r==0;
}
/* Entire-record rename avoids reliance on atomic in-place eight-byte writes.
 * Verified ONLY for suitable Linux filesystems, not a universal power-cut
 * guarantee. Same-directory atomic rename + file fsync + dir fsync needed. */
static int persist(ores_iot_checkpoint_posix *store,unsigned slot) {
    char tmp[96];
    int fd=-1,ok=0;
    unsigned tries;
    if (!store || store->dirfd < 0 || store->owner_pid!=(long)getpid() ||
        slot > 1 || !store->loaded[slot]) return 0;
    for(tries=0;tries<128;++tries) {
        int count;
        if(store->serial==ULONG_MAX) return 0;
        ++store->serial;
        count=snprintf(tmp,sizeof tmp,".slot%u.%ld.%lu.tmp",slot,
                       (long)getpid(),store->serial);
        if(count<=0 || (size_t)count>=sizeof tmp) return 0;
        fd=openat(store->dirfd,tmp,O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600);
        if(fd>=0) break;
        if(errno!=EEXIST) return 0;
    }
    if(fd<0) return 0;
    ok=write_all(fd,store->staged[slot],ORES_IOT_CHECKPOINT_BYTES) && sync_fd(fd);
    if(close(fd)!=0) ok=0;
    if(!ok) { unlinkat(store->dirfd,tmp,0); return 0; }
    if(renameat(store->dirfd,tmp,store->dirfd,slot_names[slot])!=0) {
        unlinkat(store->dirfd,tmp,0);return 0;
    }
    return sync_fd(store->dirfd);
}
static int flush_slot(void *context,unsigned slot) {
    return persist((ores_iot_checkpoint_posix*)context,slot);
}
static int publish_marker(void *context,unsigned slot,const uint8_t marker[8]) {
    ores_iot_checkpoint_posix *store=context;
    static const uint8_t unpublished[ORES_IOT_CHECKPOINT_MARKER_BYTES]={0};
    static const uint8_t valid_marker[ORES_IOT_CHECKPOINT_MARKER_BYTES]={
        'O','R','E','C','O','M','0','1'
    };
    if(!store || store->dirfd<0 || store->owner_pid!=(long)getpid() ||
       slot>1 || !store->loaded[slot] || !marker ||
       memcmp(marker,valid_marker,sizeof valid_marker)!=0 ||
       memcmp(store->staged[slot]+ORES_IOT_CHECKPOINT_MARKER_OFFSET,
              unpublished,sizeof unpublished)!=0) return 0;
    memcpy(store->staged[slot]+56,marker,8);
    return persist(store,slot);
}
ores_iot_checkpoint_io ores_iot_checkpoint_posix_adapter(
    ores_iot_checkpoint_posix *store) {
    ores_iot_checkpoint_io io={store,read_slot,write_bytes,flush_slot,publish_marker};
    return io;
}
