// Small, one-way root-owned progress state. User requests cannot name a program
// or installation target; the host alone decides whether a restart is safe.
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <math.h>
static void VIMProgressCheck(BOOL ok,NSString *message) {
    if(!ok)@throw [NSException exceptionWithName:@"GuestProgress" reason:message userInfo:nil];
}
static int VIMProgressDirectory(NSString *path) {
    NSRegularExpression *pattern=[NSRegularExpression regularExpressionWithPattern:@"^/private/var/tmp/VirtualIntelMacUI\\.[A-Za-z0-9]{6}$" options:0 error:NULL];
    VIMProgressCheck([path isKindOfClass:NSString.class]&&[pattern numberOfMatchesInString:path options:0 range:NSMakeRange(0,path.length)]==1,@"Invalid guest progress directory.");
    int fd=open(path.fileSystemRepresentation,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);struct stat st;
    if(fd<0)return -1;
    if(fstat(fd,&st)||st.st_uid!=0||(st.st_mode&0777)!=0755){close(fd);return -1;}
    return fd;
}
static NSDictionary *VIMProgressRead(int directory,NSString *name,uid_t owner) {
    int fd=openat(directory,name.fileSystemRepresentation,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);
    if(fd<0)return nil;
    @try {
        struct stat st;if(fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_nlink!=1||st.st_uid!=owner||
                          (st.st_mode&0022)||st.st_size<=0||st.st_size>32768)return nil;
        NSMutableData *data=[NSMutableData dataWithLength:(NSUInteger)st.st_size];size_t offset=0;
        while(offset<data.length){ssize_t n=read(fd,(uint8_t *)data.mutableBytes+offset,data.length-offset);
            if(n<0&&errno==EINTR)continue;if(n<=0)return nil;offset+=(size_t)n;}
        id value=[NSPropertyListSerialization propertyListWithData:data options:0 format:NULL error:NULL];
        return [value isKindOfClass:NSDictionary.class]?value:nil;
    } @finally {close(fd);}
}
static void VIMProgressWrite(int directory,NSString *name,NSDictionary *state,mode_t mode) {
    NSData *data=[NSPropertyListSerialization dataWithPropertyList:state format:NSPropertyListBinaryFormat_v1_0 options:0 error:NULL];
    VIMProgressCheck(data&&data.length<=32768,@"Invalid progress state.");
    NSString *temporary=[@".state-" stringByAppendingString:NSUUID.UUID.UUIDString];
    int fd=openat(directory,temporary.fileSystemRepresentation,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,mode);
    VIMProgressCheck(fd>=0,@"Could not create progress state.");
    @try {
        size_t offset=0;
        while(offset<data.length){ssize_t n=write(fd,(const uint8_t *)data.bytes+offset,data.length-offset);
            if(n<0&&errno==EINTR)continue;VIMProgressCheck(n>0,@"Could not write progress state.");offset+=(size_t)n;}
        VIMProgressCheck(!fchmod(fd,mode)&&!renameat(directory,temporary.fileSystemRepresentation,directory,name.fileSystemRepresentation),@"Could not publish progress state.");
    } @finally {close(fd);unlinkat(directory,temporary.fileSystemRepresentation,0);}
}
static void VIMProgressUpdate(NSString *path,NSString *mode,double fraction,NSString *message) {
    VIMProgressCheck(geteuid()==0,@"Progress updates require the guest agent.");
    VIMProgressCheck([@[@"working",@"restart",@"failed",@"complete",@"close"] containsObject:mode]&&message.length<=2048&&
                      isfinite(fraction)&&fraction>=-1&&fraction<=1,@"Invalid progress update.");
    int directory=VIMProgressDirectory(path);VIMProgressCheck(directory>=0,@"Guest progress state is unavailable.");
    @try {
        NSDictionary *old=VIMProgressRead(directory,@"State.plist",0);
        VIMProgressCheck([old[@"ConsoleUID"] isKindOfClass:NSNumber.class],@"Guest progress identity is missing.");
        NSMutableDictionary *state=[@{@"FormatVersion":@1,@"Mode":mode,@"Fraction":@(fraction),@"Message":message,
            @"Revision":NSUUID.UUID.UUIDString,@"ConsoleUID":old[@"ConsoleUID"]} mutableCopy];
        if([mode isEqual:@"restart"])state[@"Deadline"]=[NSDate dateWithTimeIntervalSinceNow:60];
        VIMProgressWrite(directory,@"State.plist",state,0644);
    } @finally {close(directory);}
}
