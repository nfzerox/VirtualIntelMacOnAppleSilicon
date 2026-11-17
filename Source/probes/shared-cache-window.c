/* Finite read-only/COW paging diagnostic. No system files are modified.
 * shared-cache-window FILE ALIGNED_OFFSET EXPECTED_SHA256
 * Map one 16 KiB cache verification window privately, touch its pages and
 * compare the original bytes. A native SIGBUS remains visible to the caller.
 */
#include <CommonCrypto/CommonDigest.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

int main(int argc,char **argv)
{
    if(argc!=4||strlen(argv[3])!=64)return 64;
    char *end;
    uint64_t offset=strtoull(argv[2],&end,0);
    long page=getpagesize();
    if(*end||offset%page||offset>INT64_MAX-16384)return 64;
    int fd=open(argv[1],O_RDONLY|O_NOFOLLOW);
    struct stat info;
    if(fd<0||fstat(fd,&info)||!S_ISREG(info.st_mode)||info.st_size<(off_t)offset+16384){
        perror("cache window");if(fd>=0)close(fd);return 1;
    }
    void *mapped=mmap(NULL,16384,PROT_READ|PROT_WRITE,MAP_PRIVATE,fd,(off_t)offset);
    close(fd);
    if(mapped==MAP_FAILED){perror("mmap");return 1;}
    // Exercise private write-fault/page-in semantics, not a file write.
    volatile unsigned char *bytes=mapped;
    for(size_t i=0;i<16384;i+=(size_t)page){unsigned char original=bytes[i];bytes[i]=original;}
    unsigned char digest[CC_SHA256_DIGEST_LENGTH];char hex[65];
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    CC_SHA256(mapped,16384,digest);
#pragma clang diagnostic pop
    for(unsigned i=0;i<sizeof(digest);i++)snprintf(hex+2*i,3,"%02x",digest[i]);
    int result=strcmp(hex,argv[3])!=0;
    printf("Private-map 16KiB SHA256: %s\nExpected digest: %s\n",hex,result?"MISMATCH":"MATCH");
    munmap(mapped,16384);
    return result;
}
