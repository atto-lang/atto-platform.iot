#ifndef ORES_IOT_POSIX_ORPHANS_INTERNAL_H
#define ORES_IOT_POSIX_ORPHANS_INTERNAL_H
/* Linux-only, internal helper. Caller holds the lifetime-long exclusive flock
 * on an owner-owned private directory. Never delete an unexpected file. */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define ORES_IOT_ORPHAN_CLEANUP_LIMIT 128u

static int ores_iot_own_temp_name(const char *name) {
    const char *p=name;
    unsigned digits;
    if (strncmp(p,".slot0.",7)==0 || strncmp(p,".slot1.",7)==0)
        p+=7;
    else return 0;
    /* Both pid and serial are emitted as positive decimal values; bound
     * digits to avoid mistaking arbitrarily long attacker names as ours. */
    for(digits=0; *p>='0' && *p<='9' && digits<20u; ++p,++digits) {}
    if (digits==0 || digits>20u || *p!='.') return 0;
    ++p;
    for(digits=0; *p>='0' && *p<='9' && digits<20u; ++p,++digits) {}
    return digits>0 && digits<=20u && strcmp(p,".tmp")==0;
}

static int ores_iot_cleanup_orphan_temps(int trusted_dirfd) {
    DIR *scan;
    struct dirent *entry;
    unsigned removed=0;
    int ok=1;
    int scanfd=openat(trusted_dirfd,".",O_RDONLY|O_CLOEXEC|O_DIRECTORY|O_NOFOLLOW);
    if(scanfd<0) return 0;
    scan=fdopendir(scanfd);
    if(!scan) { close(scanfd); return 0; }
    for(;;) {
        struct stat st;
        errno=0;
        entry=readdir(scan);
        if(!entry) { if(errno) ok=0; break; }
        if(!ores_iot_own_temp_name(entry->d_name)) continue;
        if(removed==ORES_IOT_ORPHAN_CLEANUP_LIMIT ||
           fstatat(trusted_dirfd,entry->d_name,&st,AT_SYMLINK_NOFOLLOW)!=0 ||
           !S_ISREG(st.st_mode) || st.st_uid!=geteuid() ||
           st.st_nlink!=1 || (st.st_mode & 0777)!=0600 ||
           st.st_size<0 || st.st_size>64) { ok=0; break; }
        /* No following links, and refuse all suspicious matches instead of
         * cleaning arbitrary names or foreign inode types. */
        if(unlinkat(trusted_dirfd,entry->d_name,0)!=0) { ok=0; break; }
        ++removed;
    }
    if(closedir(scan)!=0) ok=0;
    if(removed) {
        int result;
        do { result=fsync(trusted_dirfd); } while(result<0 && errno==EINTR);
        if(result!=0) ok=0;
    }
    return ok;
}
#endif
