#import <Foundation/Foundation.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <CommonCrypto/CommonDigest.h>
#include "guest-support-media.h"
static NSString *Digest(NSData *data) {
    unsigned char bytes[32];CC_SHA256(data.bytes,(CC_LONG)data.length,bytes);
    NSMutableString *text=[NSMutableString string];for(unsigned i=0;i<32;i++)[text appendFormat:@"%02x",bytes[i]];return text;
}
static int Test(NSString *archiveName) {@autoreleasepool {
    char path[]="/tmp/vim-support-tests.XXXXXX";if(!mkdtemp(path))return 1;
    NSString *root=@(path),*volumes=[root stringByAppendingPathComponent:@"Volumes"],*disc=[volumes stringByAppendingPathComponent:@"Renamed disc"];
    NSFileManager *fm=NSFileManager.defaultManager;
    [fm createDirectoryAtPath:disc withIntermediateDirectories:YES attributes:nil error:NULL];
    NSMutableDictionary *files=[NSMutableDictionary dictionary];
    for(NSString *name in @[@"guest-macho",@"guest-image-patch",@"guest-prepare",@"guest-install",@"guest-progress",@"codesign_allocate",@"SupportIcon.png",archiveName]) {
        NSData *data=[[name stringByAppendingString:@" fixture bytes"] dataUsingEncoding:NSUTF8StringEncoding];
        [data writeToFile:[disc stringByAppendingPathComponent:name] atomically:YES];
        files[name]=@{@"SHA256":Digest(data),@"Size":@(data.length)};
    }
    NSData *manifest=[NSPropertyListSerialization dataWithPropertyList:@{@"FormatVersion":@1,@"Guest":@"catalina",@"Files":files} format:NSPropertyListBinaryFormat_v1_0 options:0 error:NULL];
    [manifest writeToFile:[disc stringByAppendingPathComponent:@"Support.plist"] atomically:YES];
    for(NSString *guest in @[@"mojave",@"bigsur"]) {
        NSData *other=[NSPropertyListSerialization dataWithPropertyList:@{@"FormatVersion":@1,@"Guest":guest,@"Files":files} format:NSPropertyListBinaryFormat_v1_0 options:0 error:NULL];
        NSString *target=[root stringByAppendingPathComponent:guest];
        [fm createDirectoryAtPath:target withIntermediateDirectories:NO attributes:@{NSFilePosixPermissions:@0700} error:NULL];
        [other writeToFile:[target stringByAppendingPathComponent:@"support-manifest.plist"] atomically:YES];
        [other writeToFile:[disc stringByAppendingPathComponent:@"Support.plist"] atomically:YES];
        @try {if(![VIMStageSupportMedia(@"support-manifest.plist",target,volumes,NO)[@"SHA256Verified"] isEqual:@YES])return 1;}
        @catch(NSException *error){fprintf(stderr,"%s support copy failed\n",guest.UTF8String);return 1;}
    }
    [manifest writeToFile:[disc stringByAppendingPathComponent:@"Support.plist"] atomically:YES];
    for(unsigned test=0;test<4;test++) {
        NSString *target=[root stringByAppendingPathComponent:[NSString stringWithFormat:@"Staging%u",test]];
        [fm createDirectoryAtPath:target withIntermediateDirectories:NO attributes:@{NSFilePosixPermissions:@0700} error:NULL];
        [manifest writeToFile:[target stringByAppendingPathComponent:@"support-manifest.plist"] atomically:YES];
        if(test==1)symlink([disc stringByAppendingPathComponent:@"guest-install"].fileSystemRepresentation,[target stringByAppendingPathComponent:@"guest-install"].fileSystemRepresentation);
        if(test==2) {
            NSString *payload=[disc stringByAppendingPathComponent:archiveName];
            NSMutableData *corrupt=[[NSData dataWithContentsOfFile:payload] mutableCopy];
            ((uint8_t *)corrupt.mutableBytes)[0]^=0xff;[corrupt writeToFile:payload atomically:YES];
        }
        if(test==3)chmod(target.fileSystemRepresentation,0755);
        BOOL passed=NO;
        @try {
            NSDictionary *receipt=VIMStageSupportMedia(@"support-manifest.plist",target,volumes,NO);
            passed=test==0&&[receipt[@"SHA256Verified"] isEqual:@YES]&&[receipt[@"Files"] count]==8;
            if(test==0) {
                // A repeat only reuses files after hashing them again.
                passed&=[VIMStageSupportMedia(@"support-manifest.plist",target,volumes,NO)[@"SHA256Verified"] isEqual:@YES];
                for(NSString *name in files)passed&=[[NSData dataWithContentsOfFile:[target stringByAppendingPathComponent:name]] isEqual:[NSData dataWithContentsOfFile:[disc stringByAppendingPathComponent:name]]];
            }
        } @catch(NSException *exception) {passed=test!=0;}
        if(!passed){fprintf(stderr,"support media fixture %u failed\n",test);return 1;}
        if(test==2&&[fm fileExistsAtPath:[target stringByAppendingPathComponent:archiveName]])return 1;
    }
    puts("PASS: support copy, repeat hash verification, symlink rejection, corrupt copy not published, private ownership");return 0;
}}
int main(void) {@autoreleasepool {return Test(@"payload.tar.gz")||Test(@"payload.tar.xz");}}
