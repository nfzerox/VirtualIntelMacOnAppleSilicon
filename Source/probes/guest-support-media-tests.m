#import <Foundation/Foundation.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include "guest-support-media.h"
static int Test(NSString *archiveName) {@autoreleasepool {
    char path[]="/tmp/vim-support-tests.XXXXXX";if(!mkdtemp(path))return 1;
    NSString *root=@(path),*volumes=[root stringByAppendingPathComponent:@"Volumes"],*disc=[volumes stringByAppendingPathComponent:@"Renamed disc"];
    NSFileManager *fm=NSFileManager.defaultManager;
    [fm createDirectoryAtPath:disc withIntermediateDirectories:YES attributes:nil error:NULL];
    NSMutableDictionary *files=[NSMutableDictionary dictionary];
    for(NSString *name in @[@"guest-macho",@"guest-image-patch",@"guest-prepare",@"guest-install",@"guest-progress",@"codesign_allocate",@"SupportIcon.png",archiveName]) {
        NSData *data=[[name stringByAppendingString:@" fixture bytes"] dataUsingEncoding:NSUTF8StringEncoding];
        [data writeToFile:[disc stringByAppendingPathComponent:name] atomically:YES];
        files[name]=@{@"Size":@(data.length)};
    }
    NSData *manifest=[NSPropertyListSerialization dataWithPropertyList:@{@"FormatVersion":@1,@"Kind":@"VirtualIntelMacSupport",@"Guest":@"catalina"} format:NSPropertyListBinaryFormat_v1_0 options:0 error:NULL];
    [manifest writeToFile:[disc stringByAppendingPathComponent:@"Support.plist"] atomically:YES];
    for(NSString *guest in @[@"mojave",@"bigsur"]) {
        NSData *other=[NSPropertyListSerialization dataWithPropertyList:@{@"FormatVersion":@1,@"Kind":@"VirtualIntelMacSupport",@"Guest":guest} format:NSPropertyListBinaryFormat_v1_0 options:0 error:NULL];
        NSString *target=[root stringByAppendingPathComponent:guest];
        [fm createDirectoryAtPath:target withIntermediateDirectories:NO attributes:@{NSFilePosixPermissions:@0700} error:NULL];
        [other writeToFile:[disc stringByAppendingPathComponent:@"Support.plist"] atomically:YES];
        @try {if(![VIMStageSupportMedia(guest,target,volumes,NO)[@"FilesStaged"] isEqual:@YES])return 1;}
        @catch(NSException *error){fprintf(stderr,"%s support copy failed\n",guest.UTF8String);return 1;}
    }
    [manifest writeToFile:[disc stringByAppendingPathComponent:@"Support.plist"] atomically:YES];
    for(unsigned test=0;test<6;test++) {
        NSString *target=[root stringByAppendingPathComponent:[NSString stringWithFormat:@"Staging%u",test]];
        [fm createDirectoryAtPath:target withIntermediateDirectories:NO attributes:@{NSFilePosixPermissions:@0700} error:NULL];
        if(test==1)symlink([disc stringByAppendingPathComponent:@"guest-install"].fileSystemRepresentation,[target stringByAppendingPathComponent:@"guest-install"].fileSystemRepresentation);
        if(test==2) {
            NSString *payload=[disc stringByAppendingPathComponent:archiveName];
            NSMutableData *corrupt=[[NSData dataWithContentsOfFile:payload] mutableCopy];
            [corrupt appendData:[@" replacement with a different length" dataUsingEncoding:NSUTF8StringEncoding]];[corrupt writeToFile:payload atomically:YES];
        }
        if(test==3)chmod(target.fileSystemRepresentation,0755);
        if(test==4)truncate([disc stringByAppendingPathComponent:archiveName].fileSystemRepresentation,0);
        if(test==5){
            NSString *payload=[disc stringByAppendingPathComponent:archiveName];
            [fm removeItemAtPath:payload error:NULL];
            symlink([disc stringByAppendingPathComponent:@"guest-install"].fileSystemRepresentation,payload.fileSystemRepresentation);
        }
        BOOL passed=NO;
        @try {
            NSDictionary *receipt=VIMStageSupportMedia(@"catalina",target,volumes,NO);
            passed=(test==0||test==2)&&[receipt[@"FilesStaged"] isEqual:@YES]&&[receipt[@"Files"] count]==8&&[receipt[@"Archive"] isEqual:archiveName];
            if(test==0) {
                passed&=[VIMStageSupportMedia(@"catalina",target,volumes,NO)[@"FilesStaged"] isEqual:@YES];
                for(NSString *name in files)passed&=[[NSData dataWithContentsOfFile:[target stringByAppendingPathComponent:name]] isEqual:[NSData dataWithContentsOfFile:[disc stringByAppendingPathComponent:name]]];
            }
        } @catch(NSException *exception) {passed=test==1||test==3||test==4||test==5;}
        if(!passed){fprintf(stderr,"support media fixture %u failed\n",test);return 1;}
        if((test==4||test==5)&&[fm fileExistsAtPath:[target stringByAppendingPathComponent:archiveName]])return 1;
    }
    [fm removeItemAtPath:root error:NULL];
    puts("PASS: support copy for three OS families, gzip/xz discovery, differently sized replacements, source/target symlink rejection, empty file rejection, private ownership");return 0;
}}
int main(void) {@autoreleasepool {return Test(@"payload.tar.gz")||Test(@"payload.tar.xz");}}
