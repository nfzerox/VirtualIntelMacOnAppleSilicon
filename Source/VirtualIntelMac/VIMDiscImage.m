#import "VIMLocalization.h"
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
        DiscFailure(VIMLocalized(@"discimage.choose-a-cd-dvd-image", @"Choose a CD/DVD image"),VIMLocalized(@"discimage.choose-a-complete-iso-cdr-toast-or-unencrypted-dmg-image", @"Choose a complete ISO, CDR, Toast or unencrypted DMG image. You can eject the current disc in Configure → Hardware → CD/DVD."),error);return nil;
    }
    unsigned char header[4]={0},footer[4]={0};
    ssize_t head=pread(file,header,sizeof(header),0);
    ssize_t tail=pread(file,footer,sizeof(footer),metadata.st_size-512);
    close(file);
    if(head!=sizeof(header)||tail!=sizeof(footer)) {
        DiscFailure(VIMLocalized(@"discimage.the-disc-image-couldn-t-be-read", @"The disc image couldn’t be read"),VIMLocalized(@"discimage.make-sure-the-image-is-fully-downloaded-and-available-on", @"Make sure the image is fully downloaded and available on this Mac, then choose it again."),error);return nil;
    }
    BOOL sparse=!memcmp(header,"sprs",4);
    BOOL virtualDisk=!memcmp(header,"QFI\xfb",4)||!memcmp(header,"vhdx",4)||!memcmp(header,"KDMV",4);
    if(sparse||virtualDisk) {
        NSString *detail=[NSString stringWithFormat:VIMLocalized(@"discimage.is-choose-an-iso-cdr-toast-or-unencrypted-dmg-instead", @"“%@” is %@. Choose an ISO, CDR, Toast or unencrypted DMG instead. Your original file hasn’t been changed."),path.lastPathComponent,sparse?VIMLocalized(@"discimage.a-sparse-disk-image", @"a sparse disk image"):VIMLocalized(@"discimage.a-virtual-hard-disk", @"a virtual hard disk")];
        DiscFailure(VIMLocalized(@"discimage.this-image-can-t-be-inserted-as-a-cd-dvd", @"This image can’t be inserted as a CD/DVD"),detail,error);return nil;
    }
    return !memcmp(footer,"koly",4)||[path.pathExtension.lowercaseString isEqual:@"dmg"]?@"dmg":@"raw";
}
BOOL VIMValidateOpticalImage(NSString *path,NSError **error) {return VIMOpticalImageFormat(path,error)!=nil;}

NSError *VIMFriendlyDiscImageError(NSString *path,NSError *underlying) {
    NSString *detail=underlying.localizedDescription.lowercaseString;
    NSString *advice=VIMLocalized(@"discimage.make-sure-the-image-is-fully-downloaded-choose-an-iso", @"Make sure the image is fully downloaded. Choose an ISO, CDR, Toast or unencrypted DMG. Your original file hasn’t been changed.");
    if([underlying.domain isEqual:NSPOSIXErrorDomain]&&(underlying.code==ENOENT||underlying.code==ENOTDIR))
        advice=VIMLocalized(@"discimage.the-image-may-have-been-moved-or-deleted-or-its", @"The image may have been moved or deleted, or its drive may be disconnected. Reconnect its drive or choose the image at its current location.");
    else if([underlying.domain isEqual:NSPOSIXErrorDomain]&&(underlying.code==EACCES||underlying.code==EPERM))
        advice=VIMLocalized(@"discimage.virtual-intel-mac-can-t-read-this-image-choose-it", @"Virtual Intel Mac can’t read this image. Choose it again using Connect Image, and allow access if macOS asks.");
    else if(([underlying.domain isEqual:NSPOSIXErrorDomain]&&underlying.code==EBUSY)||
            [detail containsString:@" lock"]||[detail containsString:@"locked"])
        advice=VIMLocalized(@"discimage.close-other-apps-using-this-image", @"Close other apps using this image. If the image is mounted on this Mac, eject it, then try again.");
    else if([detail containsString:@"encrypt"]||[detail containsString:@"password"])
        advice=VIMLocalized(@"discimage.password-protected-disk-images-can-t-be-connected-to-a", @"Password-protected disk images can’t be connected to a virtual Mac. Choose an unencrypted image instead.");
    else if([detail containsString:@"no free ports"]||[detail containsString:@"not found (in use?)"])
        advice=VIMLocalized(@"discimage.this-virtual-mac-s-usb-ports-are-in-use-disconnect", @"This virtual Mac’s USB ports are in use. Disconnect another image or USB device, then try again.");
    else if([detail containsString:@"xz"]||[detail containsString:@"lzma"]||[detail containsString:@"not supported"]||[detail containsString:@"unsupported"])
        advice=VIMLocalized(@"discimage.this-image-uses-a-disk-format-or-compression-method-that", @"This image uses a disk format or compression method that Virtual Intel Mac can’t read. Choose an ISO, CDR or Toast image, or a DMG that is uncompressed or uses zlib, bzip2 or LZFSE compression. No conversion was attempted, and your original image hasn’t been changed.");
    return [NSError errorWithDomain:@"VirtualIntelMac.DiscImage" code:2 userInfo:@{
        NSLocalizedDescriptionKey:[NSString stringWithFormat:VIMLocalized(@"discimage.couldn-t-be-connected", @"“%@” couldn’t be connected"),path.lastPathComponent],
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
                userInfo:@{NSLocalizedDescriptionKey:[[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding]?:VIMLocalized(@"discimage.the-disk-image-could-not-be-read", @"The disk image could not be read.")}];
        }
        if(error)error=VIMFriendlyDiscImageError(path,error);
        dispatch_async(dispatch_get_main_queue(),^{completion(error);});
    });
}
