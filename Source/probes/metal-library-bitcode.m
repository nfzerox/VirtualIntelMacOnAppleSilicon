#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <objc/message.h>
// Inspect a donor library through Metal's parser rather than guessing MTLB
// offsets. Does not compile or submit GPU work.
int main(int argc,char **argv) {
    if(argc!=2&&argc!=4)return 64;
    @autoreleasepool {
        id<MTLDevice> device=[MTLCopyAllDevices() firstObject];NSError *error=nil;
        if(!device){fprintf(stderr,"No Metal device\n");return 5;}
        id<MTLLibrary> library=[device newLibraryWithFile:@(argv[1]) error:&error];
        if(!library){fprintf(stderr,"%s\n",error.description.UTF8String);return 1;}
        if(argc==2){for(NSString *name in library.functionNames)puts(name.UTF8String);return 0;}
        BOOL all=!strcmp(argv[2],"--all");
        if(all) {
            if([[NSFileManager defaultManager] fileExistsAtPath:@(argv[3])])return 73;
            if(![[NSFileManager defaultManager] createDirectoryAtPath:@(argv[3]) withIntermediateDirectories:NO attributes:nil error:&error])return 4;
        }
        for(NSString *name in all?library.functionNames:@[@(argv[2])]) {
        if([name containsString:@"/"]||[name isEqualToString:@".."] )return 65;
        id function=[library newFunctionWithName:name];
        SEL selector=sel_registerName("bitcodeData");
        if(!function||![function respondsToSelector:selector])return 2;
        NSData *bytes=((id(*)(id,SEL))objc_msgSend)(function,selector);
        if(![bytes isKindOfClass:[NSData class]])return 3;
        NSString *path=all?[@(argv[3]) stringByAppendingPathComponent:[name stringByAppendingString:@".bc"]]:@(argv[3]);
        if(![bytes writeToFile:path options:NSDataWritingWithoutOverwriting error:&error])return 4;
        }
        return 0;
    }
}
