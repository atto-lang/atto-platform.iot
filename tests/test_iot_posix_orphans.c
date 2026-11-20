#define _POSIX_C_SOURCE 200809L
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include "../src/ores_iot_posix_orphans_internal.h"

static void create(const char *dir,const char *file,mode_t mode,int n) {
    int fd; char name[512];
    assert(snprintf(name,sizeof name,"%s/%s",dir,file)>0);
    fd=open(name,O_WRONLY|O_CREAT|O_EXCL,0600);
    assert(fd>=0);
    while(n--) assert(write(fd,"z",1)==1);
    assert(fchmod(fd,mode)==0);
    assert(close(fd)==0);
}
static int has(const char *dir,const char *file) {
    char name[512]; struct stat st;
    assert(snprintf(name,sizeof name,"%s/%s",dir,file)>0);
    return lstat(name,&st)==0;
}
static void drop(const char *dir,const char *file) {
    char name[512];
    assert(snprintf(name,sizeof name,"%s/%s",dir,file)>0);
    assert(unlink(name)==0);
}
int main(void) {
    char dir[]="/tmp/ores-iot-orphans-XXXXXX";
    int dirfd;
    assert(mkdtemp(dir)!=NULL);
    dirfd=open(dir,O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);
    assert(dirfd>=0);
    assert(ores_iot_own_temp_name(".slot0.123.1.tmp"));
    assert(ores_iot_own_temp_name(".slot1.3.987.tmp"));
    assert(!ores_iot_own_temp_name(".slot2.123.1.tmp"));
    assert(!ores_iot_own_temp_name(".slot0.1.1.tmp.bad"));
    assert(!ores_iot_own_temp_name(".slot0.x.1.tmp"));
    assert(!ores_iot_own_temp_name("slot0.123.1.tmp"));
    create(dir,".slot0.123.1.tmp",0600,0);
    create(dir,".slot1.123.2.tmp",0600,64);
    create(dir,"unrelated.txt",0600,1);
    assert(ores_iot_cleanup_orphan_temps(dirfd));
    assert(!has(dir,".slot0.123.1.tmp"));
    assert(!has(dir,".slot1.123.2.tmp"));
    assert(has(dir,"unrelated.txt"));
    create(dir,".slot0.1.1.tmp",0644,1);
    assert(!ores_iot_cleanup_orphan_temps(dirfd));
    assert(has(dir,".slot0.1.1.tmp"));
    drop(dir,".slot0.1.1.tmp");
    {
        char name[512];
        assert(snprintf(name,sizeof name,"%s/.slot1.1.2.tmp",dir)>0);
        assert(symlink("/etc/passwd",name)==0);
        assert(!ores_iot_cleanup_orphan_temps(dirfd));
        assert(has(dir,".slot1.1.2.tmp"));
        assert(unlink(name)==0);
    }
    create(dir,".slot0.9.2.tmp",0600,65);
    assert(!ores_iot_cleanup_orphan_temps(dirfd));
    drop(dir,".slot0.9.2.tmp");
    assert(ores_iot_cleanup_orphan_temps(dirfd));
    drop(dir,"unrelated.txt");
    assert(close(dirfd)==0);
    assert(rmdir(dir)==0);
    puts("IoT POSIX orphan cleanup: PASS");
    return 0;
}
