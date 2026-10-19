#import "VIMDiscImage.h"
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

static BOOL DiscFailure(NSString *message, NSString *recovery, NSError **error) {
    if(error)*error=[NSError errorWithDomain:@"VirtualIntelMac.DiscImage" code:1
        userInfo:@{NSLocalizedDescriptionKey:message,NSLocalizedRecoverySuggestionErrorKey:recovery}];
    return NO;
}

NSString *VIMOpticalImageFormat(NSString *path, NSError **error) {
    int file=open(path.fileSystemRepresentation,O_RDONLY|O_NONBLOCK|O_CLOEXEC);
    if(file<0) {
        int reason=errno;
        if(error)*error=[NSError errorWithDomain:NSPOSIXErrorDomain code:reason
            userInfo:@{NSFilePathErrorKey:path}];
        return nil;
    }
    struct stat metadata;
    if(fstat(file,&metadata)||!S_ISREG(metadata.st_mode)||metadata.st_size<2048) {
        close(file);
        DiscFailure(@"Choose a CD/DVD image",@"Choose a complete ISO, CDR or unencrypted DMG image. You can eject the current disc in Configure → Hardware → CD/DVD.",error);return nil;
    }
    unsigned char header[4]={0},footer[4]={0};
    ssize_t head=pread(file,header,sizeof(header),0);
    ssize_t tail=pread(file,footer,sizeof(footer),metadata.st_size-512);
    close(file);
    if(head!=sizeof(header)||tail!=sizeof(footer)) {
        DiscFailure(@"The disc image couldn’t be read",@"Make sure the image is fully downloaded and available on this Mac, then choose it again.",error);return nil;
    }
    BOOL sparse=!memcmp(header,"sprs",4);
    BOOL virtualDisk=!memcmp(header,"QFI\xfb",4)||!memcmp(header,"vhdx",4)||!memcmp(header,"KDMV",4);
    if(sparse||virtualDisk) {
        NSString *detail=[NSString stringWithFormat:@"“%@” is %@. Choose an ISO, CDR or unencrypted DMG instead. Your original file hasn’t been changed.",path.lastPathComponent,sparse?@"a sparse disk image":@"a virtual hard disk"];
        DiscFailure(@"This image can’t be inserted as a CD/DVD",detail,error);return nil;
    }
    return !memcmp(footer,"koly",4)||[path.pathExtension.lowercaseString isEqual:@"dmg"]?@"dmg":@"raw";
}
BOOL VIMValidateOpticalImage(NSString *path,NSError **error) {return VIMOpticalImageFormat(path,error)!=nil;}

NSError *VIMFriendlyDiscImageError(NSString *path,NSError *underlying) {
    NSString *detail=underlying.localizedDescription.lowercaseString;
    NSString *advice=@"Make sure the image is fully downloaded. Choose an ISO, CDR or supported, unencrypted DMG. Your original file hasn’t been changed.";
    if([underlying.domain isEqual:NSPOSIXErrorDomain]&&(underlying.code==ENOENT||underlying.code==ENOTDIR))
        advice=@"The image may have been moved or deleted, or its drive may be disconnected. Reconnect its drive or choose the image at its current location.";
    else if([underlying.domain isEqual:NSPOSIXErrorDomain]&&(underlying.code==EACCES||underlying.code==EPERM))
        advice=@"Virtual Intel Mac can’t read this image. Choose it again using Connect Image, and allow access if macOS asks.";
    else if([detail containsString:@"encrypt"]||[detail containsString:@"password"])
        advice=@"Password-protected disk images can’t be connected to a virtual Mac. Choose an unencrypted image instead.";
    else if([detail containsString:@"xz"]||[detail containsString:@"lzma"]||[detail containsString:@"not supported"]||[detail containsString:@"unsupported"])
        advice=@"This image uses a disk format or compression method that Virtual Intel Mac can’t read. Choose an ISO, CDR, or a DMG saved as read-only or zlib compressed. No conversion was attempted, and your original image hasn’t been changed.";
    return [NSError errorWithDomain:@"VirtualIntelMac.DiscImage" code:2 userInfo:@{
        NSLocalizedDescriptionKey:[NSString stringWithFormat:@"“%@” couldn’t be connected",path.lastPathComponent],
        NSLocalizedRecoverySuggestionErrorKey:advice,NSUnderlyingErrorKey:underlying}];
}
void VIMCheckOpticalImage(NSString *path,NSString *format,NSString *tool,void (^completion)(NSError *)) {
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED,0),^{
        NSTask *task=[NSTask new];task.executableURL=[NSURL fileURLWithPath:tool];
        task.arguments=@[@"info",@"--output=json",@"-f",format,@"--",path];
        NSPipe *pipe=[NSPipe pipe];task.standardOutput=pipe;task.standardError=pipe;
        task.standardInput=NSFileHandle.fileHandleWithNullDevice;
        NSError *error=nil;
        if([task launchAndReturnError:&error]) {
            dispatch_after(dispatch_time(DISPATCH_TIME_NOW,15*NSEC_PER_SEC),dispatch_get_global_queue(QOS_CLASS_UTILITY,0),^{if(task.running)[task terminate];});
            NSData *data=[pipe.fileHandleForReading readDataOfLength:65536];
            if(data.length>=65536&&task.running)[task terminate];
            [task waitUntilExit];
            if(task.terminationStatus)error=[NSError errorWithDomain:@"VirtualIntelMac.ImageCheck" code:task.terminationStatus
                userInfo:@{NSLocalizedDescriptionKey:[[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding]?:@"The disk image could not be read."}];
        }
        if(error)error=VIMFriendlyDiscImageError(path,error);
        dispatch_async(dispatch_get_main_queue(),^{completion(error);});
    });
}
