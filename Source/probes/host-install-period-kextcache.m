#import "../tools/HostInstall/VIMPeriodKextcache.h"
int main(int argc,const char *argv[]) { @autoreleasepool {
    if(argc==3&&!strcmp(argv[1],"--volume-root")) {
        NSError *error=nil;NSString *path=VIMOfflineKextcacheVolumePath([NSURL fileURLWithPath:@(argv[2])],&error);
        if(!path){fprintf(stderr,"%s\n",error.localizedDescription.UTF8String);return 1;}
        puts(path.fileSystemRepresentation);return 0;
    }
    if(argc!=2)return 64;
    NSError *error=nil;BOOL redirected=NO;
    if(!VIMPrepareOfflineKextcache([NSURL fileURLWithPath:@(argv[1])],&redirected,&error)) {
        fprintf(stderr,"%s\n",error.localizedDescription.UTF8String);return 1;
    }
    printf("%s\n",redirected?"redirected":"unchanged");return 0;
}}
