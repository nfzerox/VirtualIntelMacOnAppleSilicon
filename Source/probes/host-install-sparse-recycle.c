// Bounded scratch-volume control for discard/reuse on DiskImages sparsebundles.
// Write into a NEW directory; detach/remount the image, then use --verify.
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

enum { Files=8, Chunk=65536, Length=16*1024*1024, Rounds=3 };
static void pattern(uint8_t *bytes,unsigned file,unsigned round,unsigned offset) {
    uint32_t x=0x973ac801u^(file*65537u)^(round*104729u)^offset;
    for(unsigned i=0;i<Chunk;i++){x^=x<<13;x^=x>>17;x^=x<<5;bytes[i]=(uint8_t)x;}
}
static int access_file(int directory,unsigned file,unsigned round,int writing) {
    char name[32];snprintf(name,sizeof(name),"File-%u",file);
    int fd=openat(directory,name,(writing?O_WRONLY|O_CREAT|O_TRUNC:O_RDONLY)|O_NOFOLLOW,0600);
    if(fd<0)return 1;
    uint8_t bytes[Chunk],expected[Chunk];int problem=0;
    for(unsigned offset=0;offset<Length;offset+=Chunk) {
        pattern(expected,file,round,offset);
        ssize_t size=writing?write(fd,expected,Chunk):read(fd,bytes,Chunk);
        if(size!=Chunk||(!writing&&memcmp(bytes,expected,Chunk))) {
            fprintf(stderr,"%s %s at 0x%x (%zd bytes, errno %d)\n",
                writing?"write":"mismatch",name,offset,size,errno);problem=1;break;
        }
    }
    if(writing&&fsync(fd))problem=1;
    close(fd);return problem;
}
int main(int argc,char **argv) {
    if(argc!=3||argv[2][0]!='/'||
       (strcmp(argv[1],"--write")&&strcmp(argv[1],"--verify")))return 64;
    int writing=!strcmp(argv[1],"--write");
    if(writing&&mkdir(argv[2],0700)){perror("new control directory");return 1;}
    int directory=open(argv[2],O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
    if(directory<0)return 1;
    int problem=0;
    if(writing)for(unsigned round=0;round<Rounds&&!problem;round++) {
        for(unsigned file=0;file<Files&&!problem;file++) {
            if(round){char name[32];snprintf(name,sizeof(name),"File-%u",file);if(unlinkat(directory,name,0)){problem=1;break;}}
            problem=access_file(directory,file,round,1);
        }
        fprintf(stderr,"write round %u complete\n",round);
    }
    for(unsigned file=0;file<Files&&!problem;file++)problem=access_file(directory,file,Rounds-1,0);
    close(directory);
    puts(problem?"FAIL sparsebundle discard/reuse":"PASS sparsebundle discard/reuse");
    return problem;
}
