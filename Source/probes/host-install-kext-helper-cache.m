#import "../tools/HostInstall/VIMKextHelperCache.h"
int main(int argc,const char *argv[]) { @autoreleasepool {
    if(argc!=3&&argc!=4)return 64;
    NSError *error=nil;
    NSData *data=VIMKextHelperCache([NSURL fileURLWithPath:@(argv[1])],@(argv[2]),argc==3,&error);
    if(!data){fprintf(stderr,"%s\n",error.localizedDescription.UTF8String);return 1;}
    [NSFileHandle.fileHandleWithStandardOutput writeData:data];return 0;
}}
