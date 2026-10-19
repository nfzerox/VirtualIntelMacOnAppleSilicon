// Exercise the production media worker using an already assembled installer.
// Writes only a fresh caller-specified job. No downloads, VM boot, or host root.
#import <Foundation/Foundation.h>
#import "VIMInstallerAssembly.h"
int main(int argc,const char **argv) {@autoreleasepool {
    if(argc!=4)return 64;
    VIMInstallerAssembly *worker=[VIMInstallerAssembly new];
    worker.progress=^(NSString *stage){fprintf(stderr,"%s\n",stage.UTF8String);};
    worker.workProgress=^(double fraction){fprintf(stderr,"progress %.3f\n",fraction);};
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED,0),^{
        NSError *error=nil;NSURL *media=[worker createLegacyMediaFromInstaller:[NSURL fileURLWithPath:@(argv[1])] guest:@(argv[2]) jobDirectory:[NSURL fileURLWithPath:@(argv[3])] error:&error];
        fprintf(stderr,"%s\n",media?media.path.UTF8String:error.description.UTF8String);exit(media?0:1);
    });
    dispatch_main();
}}
