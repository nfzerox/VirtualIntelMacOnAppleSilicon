// Read-only guest I/O diagnostic, not a signature/security bypass.
// Compare the same regular file and byte range across controller types.
// Usage: uncached-read-bench FILE BLOCK_BYTES READ_COUNT cached|uncached
#include <sys/stat.h>
#include <sys/resource.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <time.h>

static uint64_t nanos(void) {
    struct timespec t;
    if(clock_gettime(CLOCK_MONOTONIC,&t)){perror("clock_gettime");exit(1);}
    return (uint64_t)t.tv_sec*1000000000ULL+t.tv_nsec;
}
static uint64_t number(const char *text) {
    errno=0;char *end=NULL;unsigned long long value=strtoull(text,&end,10);
    if(errno||!text[0]||*end||text[0]=='-'){fprintf(stderr,"Invalid number\n");exit(2);}
    return value;
}
static double seconds(struct timeval value){return value.tv_sec+value.tv_usec/1e6;}
static int compare(const void *a,const void *b) {
    uint64_t x=*(const uint64_t *)a,y=*(const uint64_t *)b;return (x>y)-(x<y);
}
int main(int argc,char **argv) {
    if(argc!=5){fprintf(stderr,"Usage: uncached-read-bench FILE BLOCK_BYTES READ_COUNT cached|uncached\n");return 2;}
    uint64_t block=number(argv[2]),count=number(argv[3]);
    int uncached=!strcmp(argv[4],"uncached");
    if((!uncached&&strcmp(argv[4],"cached"))||block<512||block>1024*1024||
       (block&(block-1))||!count||count>262144||count>256*1024*1024ULL/block) {
        fprintf(stderr,"Use power-of-two blocks from 512 B to 1 MiB, at most 256 MiB total\n");return 2;
    }
    int fd=open(argv[1],O_RDONLY|O_NOFOLLOW|O_CLOEXEC);struct stat info;
    if(fd<0){perror("open");return 1;}
    if(fstat(fd,&info)||!S_ISREG(info.st_mode)||info.st_size<0||(uint64_t)info.st_size<block*count) {
        fprintf(stderr,"Need a regular file containing the requested range\n");close(fd);return 2;
    }
    if(fcntl(fd,F_NOCACHE,uncached)){perror("F_NOCACHE");close(fd);return 1;}
    void *buffer=NULL;uint64_t *latency=calloc((size_t)count,sizeof(*latency));
    if(posix_memalign(&buffer,(size_t)getpagesize(),(size_t)block)||!latency) {
        fprintf(stderr,"Allocation failed\n");close(fd);free(buffer);free(latency);return 1;
    }
    struct rusage before,after;getrusage(RUSAGE_SELF,&before);uint64_t start=nanos();
    for(uint64_t i=0;i<count;i++) {
        uint64_t began=nanos();ssize_t got;
        do {got=read(fd,buffer,(size_t)block);}while(got<0&&errno==EINTR);
        latency[i]=nanos()-began;
        if(got!=(ssize_t)block){fprintf(stderr,"Read failed/short (%lld)\n",(long long)got);close(fd);free(buffer);free(latency);return 1;}
    }
    double elapsed=(nanos()-start)/1e9;getrusage(RUSAGE_SELF,&after);close(fd);
    qsort(latency,(size_t)count,sizeof(*latency),compare);
    printf("{\"Mode\":\"%s\",\"BlockBytes\":%llu,\"Reads\":%llu,\"Bytes\":%llu,"
           "\"Seconds\":%.6f,\"UserSeconds\":%.6f,\"SystemSeconds\":%.6f,\"MiBPerSecond\":%.3f,"
           "\"ReadP50Microseconds\":%.3f,\"ReadP95Microseconds\":%.3f,\"ReadP99Microseconds\":%.3f}\n",
           argv[4],(unsigned long long)block,(unsigned long long)count,(unsigned long long)(block*count),
           elapsed,seconds(after.ru_utime)-seconds(before.ru_utime),seconds(after.ru_stime)-seconds(before.ru_stime),
           block*count/(1024.0*1024.0*elapsed),latency[count/2]/1e3,latency[(count-1)*95/100]/1e3,latency[(count-1)*99/100]/1e3);
    free(buffer);free(latency);return 0;
}
