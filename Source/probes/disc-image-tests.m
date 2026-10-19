#import "VIMDiscImage.h"
#include <sys/stat.h>
int main(int argc,char **argv) {
    @autoreleasepool {
        NSString *directory=[NSTemporaryDirectory() stringByAppendingPathComponent:[@"vim-disc-check-" stringByAppendingString:NSUUID.UUID.UUIDString]];
        NSFileManager *files=NSFileManager.defaultManager;
        NSCAssert([files createDirectoryAtPath:directory withIntermediateDirectories:NO attributes:nil error:nil],@"fixture directory");
        NSMutableData *image=[NSMutableData dataWithLength:65536];
        memcpy((char *)image.mutableBytes+32769,"CD001",5);
        NSString *raw=[directory stringByAppendingPathComponent:@"Disc.iso"];
        NSCAssert([image writeToFile:raw atomically:YES]&&VIMValidateOpticalImage(raw,NULL),@"ordinary raw image");
        NSString *udif=[directory stringByAppendingPathComponent:@"Renamed.iso"];
        memcpy((char *)image.mutableBytes+image.length-512,"koly",4);
        NSCAssert([image writeToFile:udif atomically:YES],@"UDIF fixture");
        NSError *error=nil;
        NSCAssert([VIMOpticalImageFormat(udif,&error) isEqual:@"dmg"],@"Renamed UDIF must use the DMG driver, never raw sectors");
        NSCAssert([[NSData dataWithContentsOfFile:udif] isEqual:image],@"validation never changes the user's image");
        memset((char *)image.mutableBytes+image.length-512,0,4);
        memcpy(image.mutableBytes,"QFI\xfb",4);
        NSCAssert([image writeToFile:raw atomically:YES]&&!VIMValidateOpticalImage(raw,NULL),@"QCOW is not optical raw");
        NSCAssert(!VIMValidateOpticalImage(directory,NULL),@"directories refused");
        NSString *pipe=[directory stringByAppendingPathComponent:@"Pipe.iso"];
        NSCAssert(mkfifo(pipe.fileSystemRepresentation,0600)==0&&!VIMValidateOpticalImage(pipe,NULL),@"special files refused without blocking");
        NSCAssert(!VIMValidateOpticalImage([directory stringByAppendingPathComponent:@"Missing.iso"],NULL),@"missing file refused");
        if(argc==2)NSCAssert([VIMOpticalImageFormat(@(argv[1]),NULL) isEqual:@"dmg"],@"Real Mac disk image uses its direct driver");
        [files removeItemAtPath:directory error:nil];
        puts("PASS optical container checks, unchanged originals and nonblocking special-file rejection");
    }
}
