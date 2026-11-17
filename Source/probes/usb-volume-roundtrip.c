// Finite integrity check on an explicitly selected mounted USB volume.
// Creates/removes only its own unique directory; never opens existing files.
#include <errno.h>
#include <fcntl.h>
#include <mach/mach_time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <unistd.h>

enum { BlockBytes=1024*1024, Blocks=32 };
static void pattern(unsigned char *bytes,unsigned block) {
    for(unsigned i=0;i<BlockBytes;i++)bytes[i]=(unsigned char)((i*13)^(block*29)^(i>>12));
}
static int transfer(int fd,unsigned char *bytes,int writing) {
    size_t done=0;
    while(done<BlockBytes) {
        ssize_t n=writing?write(fd,bytes+done,BlockBytes-done):read(fd,bytes+done,BlockBytes-done);
        if(n<0&&errno==EINTR)continue;
        if(n<=0){if(!n)errno=EIO;return -1;}
        done+=(size_t)n;
    }
    return 0;
}
int main(int argc,char **argv) {
    if(argc!=2)return 64;
    struct statfs fs;
    if(statfs(argv[1],&fs)||!(fs.f_flags&MNT_LOCAL)||strcmp(argv[1],fs.f_mntonname)||!strcmp(argv[1],"/")) {
        fputs("Choose the root of the mounted test volume, not the system disk.\n",stderr);return 64;
    }
    char directory[4096],file[4096];
    if(snprintf(directory,sizeof(directory),"%s/VIM-USB-Roundtrip.XXXXXX",argv[1])>=(int)sizeof(directory))return 64;
    if(!mkdtemp(directory)){perror("test directory");return 1;}
    if(snprintf(file,sizeof(file),"%s/Data",directory)>=(int)sizeof(file))return 64;
    printf("Owned fixture: %s\n",directory);fflush(stdout);
    unsigned char *bytes=malloc(BlockBytes),*expected=malloc(BlockBytes);
    if(!bytes||!expected)return 1;
    int fd=open(file,O_CREAT|O_EXCL|O_RDWR,0600);
    if(fd<0){perror("fixture file");return 1;}
    int uncached=fcntl(fd,F_NOCACHE,1)==0;
    mach_timebase_info_data_t clock;mach_timebase_info(&clock);
    double secondsPerTick=1e-9*(double)clock.numer/clock.denom;
    uint64_t start=mach_absolute_time();
    for(unsigned block=0;block<Blocks;block++){
        pattern(bytes,block);if(transfer(fd,bytes,1))goto failed;
    }
    if(fsync(fd))goto failed;
    double writeSeconds=(mach_absolute_time()-start)*secondsPerTick;
    if(lseek(fd,0,SEEK_SET)!=0)goto failed;
    start=mach_absolute_time();
    for(unsigned block=0;block<Blocks;block++){
        pattern(expected,block);
        if(transfer(fd,bytes,0))goto failed;
        if(memcmp(bytes,expected,BlockBytes)){errno=EIO;goto failed;}
    }
    double readSeconds=(mach_absolute_time()-start)*secondsPerTick;
    if(close(fd)||unlink(file)||rmdir(directory))goto failed;
    free(bytes);free(expected);
    printf("PASS: 32 MiB written, fsynced and verified; uncached=%d write=%.3fs read=%.3fs\n",uncached,writeSeconds,readSeconds);
    return 0;
failed:
    perror("USB integrity test failed; owned fixture retained");return 1;
}
