// Match DiskImages' cached-small/uncached-large band writes, but report a
// positive short pwrite rather than silently acknowledging the full request.
// The path must name a NEW disposable file, not an image or mounted device.
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

int main(int argc,char **argv) {
    if(argc!=2||argv[1][0]!='/')return 64;
    int fd=open(argv[1],O_RDWR|O_CREAT|O_EXCL|O_NOFOLLOW,0600);
    if(fd<0){perror("new band control");return 1;}
    void *base=NULL;
    if(posix_memalign(&base,16384,2*1024*1024))return 1;
    uint8_t *bytes=base;
    uint32_t state=0xa73e910b;
    for(unsigned i=0;i<2*1024*1024;i++){state^=state<<13;state^=state>>17;state^=state<<5;bytes[i]=(uint8_t)state;}
    unsigned sizes[]={65536,131072,327680,1048576};
    int problem=0;
    for(unsigned round=0;round<32&&!problem;round++) {
        if(ftruncate(fd,0)){problem=1;break;}
        off_t offset=0;
        while(offset<8*1024*1024) {
            size_t size=sizes[round%4];
            if(size>(size_t)(8*1024*1024-offset))size=(size_t)(8*1024*1024-offset);
            if(fcntl(fd,F_NOCACHE,size>65536)){perror("F_NOCACHE");problem=1;break;}
            ssize_t n=pwrite(fd,bytes+(round%3)*4096,size,offset);
            if(n!=(ssize_t)size) {
                fprintf(stderr,"round=%u offset=0x%llx requested=%zu returned=%zd errno=%d\n",
                    round,(unsigned long long)offset,size,n,errno);problem=1;break;
            }
            offset+=size;
        }
        if(fsync(fd)){problem=1;break;}
    }
    free(base);close(fd);
    puts(problem?"FAIL band short-write control":"PASS band short-write control");return problem;
}
