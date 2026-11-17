// Development-only finalization of one inode in a private, detached APFS image.
#import "../tools/HostInstall/VIMAPFSFirmlinks.h"
#import "../tools/HostInstall/VIMHFSCatalog.h"
#include <sys/stat.h>

int main(int argc,char **argv) { @autoreleasepool {
    BOOL directory=argc>1&&!strcmp(argv[1],"--directory");
    if(directory){argc--;argv++;}
    if(argc!=4&&argc!=6)return 64;
    NSURL *raw=[NSURL fileURLWithPath:@(argv[1])];
    NSUUID *volume=[[NSUUID alloc] initWithUUIDString:@(argv[2])];
    char *end=NULL;uint64_t inode=strtoull(argv[3],&end,10);
    if(!volume||!inode||!*argv[3]||*end)return 64;
    uint64_t offset=0,size=0;NSError *error=nil;
    BOOL geometry=NO;
    if(argc==6) {
        offset=strtoull(argv[4],&end,10);if(!*argv[4]||*end)return 64;
        size=strtoull(argv[5],&end,10);if(!*argv[5]||*end)return 64;
        geometry=YES;
    } else geometry=VIMGetAPFSPartition(raw,&offset,&size,&error);
    BOOL ok=geometry&&VIMFinalizeAPFSVolume(raw,offset,size,volume,0,nil,
            @{@(inode):@[@0,@0,@(directory?(S_IFDIR|0600):(S_IFLNK|0755))]},&error);
    if(!ok)fprintf(stderr,"%s\n",error.localizedDescription.UTF8String);
    return ok?0:1;
}}
