// One-time development migration for pre-app imported Mojave disks only.
// Fresh app installs already publish the absolute helper. No Apple binary,
// policy, quarantine attribute or boot cache is changed by this tool.
#import <Foundation/Foundation.h>
#import "../tools/VIMFileTransaction.h"
#include <mach-o/loader.h>
#include <libkern/OSByteOrder.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <dlfcn.h>

static void Require(BOOL value,NSString *reason) {
    if(!value)@throw [NSException exceptionWithName:@"LegacyRepairRefused" reason:reason userInfo:nil];
}
static NSData *ReadTrusted(NSString *path,NSUInteger limit) {
    int fd=open(path.fileSystemRepresentation,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);
    Require(fd>=0,[@"Cannot open " stringByAppendingString:path]);
    struct stat info;
    BOOL safe=!fstat(fd,&info)&&S_ISREG(info.st_mode)&&info.st_uid==0&&info.st_nlink==1&&
        !(info.st_mode&0022)&&info.st_size>0&&(uint64_t)info.st_size<=limit;
    if(!safe){close(fd);Require(NO,@"Expected a bounded, root-owned regular file");}
    NSMutableData *data=[NSMutableData dataWithLength:(NSUInteger)info.st_size];NSUInteger offset=0;
    while(offset<data.length){ssize_t n=read(fd,(uint8_t *)data.mutableBytes+offset,data.length-offset);
        if(n<=0){close(fd);Require(NO,@"Cannot read the complete trusted file");}offset+=(NSUInteger)n;}
    close(fd);return data;
}
static NSString *KernelBuild(void) {
    char build[256]={0};size_t size=sizeof(build);
    Require(!sysctlbyname("kern.osversion",build,&size,NULL,0)&&size>1&&size<sizeof(build),@"Cannot read the running kernel build");
    return @(build);
}
static NSData *IntelSlice(NSData *data) {
    Require(data.length>=sizeof(struct mach_header_64),@"Truncated Mach-O image");
    uint32_t magic=OSReadBigInt32(data.bytes,0);
    if(magic==0xcafebabe||magic==0xcafebabf) {
        BOOL wide=magic==0xcafebabf;uint32_t count=OSReadBigInt32(data.bytes,4);
        NSUInteger stride=wide?32:20;Require(count>0&&count<=64&&8+count*stride<=data.length,@"Invalid universal header");
        NSData *found=nil;
        for(NSUInteger i=0;i<count;i++) {
            NSUInteger at=8+i*stride;
            if(OSReadBigInt32(data.bytes,at)!=CPU_TYPE_X86_64)continue;
            uint64_t start=wide?OSReadBigInt64(data.bytes,at+8):OSReadBigInt32(data.bytes,at+8);
            uint64_t size=wide?OSReadBigInt64(data.bytes,at+16):OSReadBigInt32(data.bytes,at+12);
            Require(!found&&size>=sizeof(struct mach_header_64)&&start<=data.length&&size<=data.length-start,@"Invalid or duplicate Intel slice");
            found=[data subdataWithRange:NSMakeRange((NSUInteger)start,(NSUInteger)size)];
        }
        Require(found!=nil,@"No Intel slice");data=found;
    }
    Require(OSReadLittleInt32(data.bytes,0)==MH_MAGIC_64&&OSReadLittleInt32(data.bytes,4)==CPU_TYPE_X86_64,@"Expected an Intel Mach-O image");
    return data;
}
static BOOL HasDependency(NSData *data,const char *wanted) {
    data=IntelSlice(data);struct mach_header_64 header;memcpy(&header,data.bytes,sizeof(header));
    Require(header.sizeofcmds<=data.length-sizeof(header),@"Invalid load-command bounds");
    NSUInteger at=sizeof(header),end=at+header.sizeofcmds,matches=0;
    for(uint32_t i=0;i<header.ncmds;i++) {
        Require(at<=end&&sizeof(struct load_command)<=end-at,@"Truncated load command");
        struct load_command command;memcpy(&command,(const uint8_t *)data.bytes+at,sizeof(command));
        Require(command.cmdsize>=sizeof(command)&&command.cmdsize<=end-at,@"Invalid load command");
        if(command.cmd==LC_LOAD_DYLIB) {
            struct dylib_command dylib;Require(command.cmdsize>=sizeof(dylib),@"Short dylib command");
            memcpy(&dylib,(const uint8_t *)data.bytes+at,sizeof(dylib));
            Require(dylib.dylib.name.offset>=sizeof(dylib)&&dylib.dylib.name.offset<command.cmdsize,@"Invalid dependency name");
            const char *name=(const char *)data.bytes+at+dylib.dylib.name.offset;
            Require(memchr(name,0,command.cmdsize-dylib.dylib.name.offset)!=NULL,@"Unterminated dependency");
            if(!strcmp(name,wanted))matches++;
        }
        at+=command.cmdsize;
    }
    Require(at==end&&matches<=1,@"Ambiguous dependency commands");return matches==1;
}
int main(int argc,const char **argv){@autoreleasepool {@try {
    Require(argc==2&&(!strcmp(argv[1],"--check")||!strcmp(argv[1],"--apply")),@"Usage: repair-legacy-mojave-helper --check|--apply");
    BOOL apply=!strcmp(argv[1],"--apply");
    NSDictionary *version=[NSDictionary dictionaryWithContentsOfFile:@"/System/Library/CoreServices/SystemVersion.plist"];
    Require([version[@"ProductVersion"] hasPrefix:@"10.14."]&&[KernelBuild() isEqual:version[@"ProductBuildVersion"]],@"Run only in an installed matching Mojave guest, never on the host");
    Require(geteuid()==0,@"Root guest access is required");
    int (*csrCheck)(uint32_t)=dlsym(RTLD_DEFAULT,"csr_check");
    Require(csrCheck&&csrCheck(1U<<1)==0,@"Guest unrestricted-filesystem SIP permission is required");
    Require([NSFileManager.defaultManager fileExistsAtPath:@"/private/var/db/.AppleSetupDone"],@"Complete guest setup first");
    for(NSString *pending in @[@"/macOS Install Data",@"/OS X Install Data",@"/System/Installation/CDIS/macOS Utilities.app"])
        Require(![NSFileManager.defaultManager fileExistsAtPath:pending],@"Finish the guest update/installation first");
    NSString *sky=@"/System/Library/PrivateFrameworks/SkyLight.framework/Versions/A/SkyLight";
    Require(HasDependency(ReadTrusted(sky,64*1024*1024),"@loader_path/PVGGLIOKit.dylib"),@"Not the legacy loader-relative SkyLight layout; no files changed");
    NSString *source=@"/System/Library/PrivateFrameworks/SkyLight.framework/Versions/A/PVGGLIOKit.dylib";
    NSString *target=@"/usr/lib/PVGGLIOKit.dylib";NSData *bytes=ReadTrusted(source,1024*1024);
    NSData *thin=IntelSlice(bytes);Require(OSReadLittleInt32(thin.bytes,12)==MH_DYLIB,@"Expected the existing compatibility dylib");
    NSTask *signature=[NSTask new];signature.launchPath=@"/usr/bin/codesign";signature.arguments=@[@"--verify",@"--strict",source];
    [signature launch];[signature waitUntilExit];Require(signature.terminationStatus==0,@"The existing helper's signature does not verify");
    struct stat targetInfo;BOOL exists=lstat(target.fileSystemRepresentation,&targetInfo)==0;
    if(exists)Require([ReadTrusted(target,1024*1024) isEqual:bytes],@"An unexpected destination already exists; it was not overwritten");
    NSMutableDictionary *result=[@{@"Guest":@"mojave",@"ProductBuildVersion":version[@"ProductBuildVersion"],
        @"HelperVerified":@(exists),@"Changed":@NO,@"Source":source,@"Target":target} mutableCopy];
    if(apply&&!exists) {
        for(NSString *parent in @[@"/usr",@"/usr/lib",@"/private/var/db"]) {
            struct stat st;Require(!lstat(parent.fileSystemRepresentation,&st)&&S_ISDIR(st.st_mode)&&st.st_uid==0&&!(st.st_mode&0022),@"Unsafe system parent directory");
        }
        char directory[]="/private/var/db/VirtualIntelMac.LegacySurface.XXXXXX";
        Require(mkdtemp(directory)!=NULL,@"Cannot create private repair staging");
        NSURL *base=[NSURL fileURLWithPath:@(directory)],*payload=[base URLByAppendingPathComponent:@"Payload"];
        NSURL *replacement=[payload URLByAppendingPathComponent:@"usr/lib/PVGGLIOKit.dylib"],*journal=[base URLByAppendingPathComponent:@"Transaction.plist"];
        NSError *error=nil;
        Require([NSFileManager.defaultManager createDirectoryAtURL:replacement.URLByDeletingLastPathComponent withIntermediateDirectories:YES attributes:@{NSFilePosixPermissions:@0700} error:&error],error.localizedDescription);
        Require([bytes writeToURL:replacement options:NSDataWritingWithoutOverwriting error:&error],error.localizedDescription);
        Require(!chmod(replacement.fileSystemRepresentation,0755),@"Cannot set helper permissions");
        Require([[NSDictionary dictionaryWithContentsOfFile:@"/System/Library/CoreServices/SystemVersion.plist"] isEqual:version]&&[KernelBuild() isEqual:version[@"ProductBuildVersion"]],@"Guest changed during staging; nothing published");
        // Never replace an existing library, even if it appeared during staging.
        Require(lstat(target.fileSystemRepresentation,&targetInfo)!=0&&errno==ENOENT,@"Destination appeared; nothing published");
        VIMFileTransaction *transaction=[VIMFileTransaction createAtURL:journal root:[NSURL fileURLWithPath:@"/"] payload:payload
            targets:@[@"usr/lib/PVGGLIOKit.dylib"] metadata:@{@"Operation":@"legacy-surface-helper-alias",@"ProductBuildVersion":version[@"ProductBuildVersion"]} error:&error];
        Require(transaction!=nil,error.localizedDescription);
        Require([transaction.state[@"Entries"] count]==1&&
            [transaction.state[@"Entries"][0][@"OriginalPresent"] isEqual:@NO],@"Destination appeared while planning; nothing published");
        Require([transaction publish:&error]&&[transaction verifyPublished:&error],error.localizedDescription);
        Require([ReadTrusted(target,1024*1024) isEqual:bytes],@"Published helper differs from the original");
        result[@"HelperVerified"]=@YES;result[@"Changed"]=@YES;result[@"GuestJournal"]=journal.path;
    }
    NSData *json=[NSJSONSerialization dataWithJSONObject:result options:NSJSONWritingPrettyPrinted error:NULL];
    printf("%s\n",[[[NSString alloc] initWithData:json encoding:NSUTF8StringEncoding] UTF8String]);return 0;
} @catch(NSException *error){fprintf(stderr,"%s\n",error.reason.UTF8String);return 1;}}}
