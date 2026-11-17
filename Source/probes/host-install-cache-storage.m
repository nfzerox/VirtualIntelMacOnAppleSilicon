// Exercise the native decoder against isolated signed cache fixtures.
// This does not modify or publish a guest disk.
#import "../tools/HostInstall/VIMBigSurCacheStorage.m"
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

int main(int argc,char **argv) { @autoreleasepool {
    BOOL plain=argc==5&&!strcmp(argv[4],"--plain");
    if(argc!=4&&!plain)return 64;
    char *end=NULL;unsigned long long logical=strtoull(argv[2],&end,10);
    if(!logical||!end||*end)return 64;
    int fd=open(argv[1],O_RDONLY|O_NOFOLLOW|O_CLOEXEC);
    struct stat info;
    if(fd<0||fstat(fd,&info)||!S_ISREG(info.st_mode)||info.st_size<=0)return 66;
    NSData *encoded=[NSData dataWithContentsOfURL:[NSURL fileURLWithPath:@(argv[3])]];
    if(!encoded.length||encoded.length%44){close(fd);return 65;}
    NSMutableData *digests=[NSMutableData data];
    const uint8_t *p=encoded.bytes;
    for(NSUInteger i=0;i<encoded.length;i+=44) {
        CacheDigest digest={.offset=U64(p+i),.length=U32(p+i+8)};
        memcpy(digest.digest,p+i+12,32);
        [digests appendBytes:&digest length:sizeof(digest)];
    }
    CacheExtent extent={0,((uint64_t)info.st_size+Block-1)&~(uint64_t)(Block-1),1};
    NSMutableData *extents=[NSMutableData dataWithBytes:&extent length:sizeof(extent)];
    Cache cache={.logical=logical,.compressed=(uint64_t)info.st_size,.digests=digests,.extents=extents};
    BOOL (^read)(uint64_t,void *,size_t)=^BOOL(uint64_t at,void *out,size_t length){
        return at>=Block&&pread(fd,out,length,(off_t)(at-Block))==(ssize_t)length;
    };
    BOOL valid=plain?VerifyPlain(cache,1+extent.length/Block,read,nil):
        VerifyCache(cache,1+extent.length/Block,read,nil);
    close(fd);
    puts(valid?"PASS signed logical cache bytes":"FAIL signed logical cache bytes");
    return valid?0:1;
}}
