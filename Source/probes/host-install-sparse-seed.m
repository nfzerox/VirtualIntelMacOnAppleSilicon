#import "../tools/HostInstall/VIMHFSCatalog.h"
#include <sys/stat.h>
int main(int argc,const char *argv[]) { @autoreleasepool {
    if(argc!=3&&argc!=5)return 64;
    NSError *error=nil;NSURL *file=[NSURL fileURLWithPath:@(argv[argc==5?3:1])];
    if(argc==5) {
        if(strcmp(argv[1],"--from-bundle"))return 64;
        NSURL *bundle=[NSURL fileURLWithPath:@(argv[2])];
        NSDictionary *info=[NSDictionary dictionaryWithContentsOfURL:[bundle URLByAppendingPathComponent:@"Info.plist"]];
        if(!VIMExportSparseBundleRange(bundle,0,[info[@"size"] unsignedLongLongValue],file,&error)) {
            fprintf(stderr,"%s\n",error.localizedDescription.UTF8String);return 1;
        }
    }
    if(!VIMExtendSparseInstallSeed(file,strtoull(argv[argc==5?4:2],NULL,10)<<30,&error)) {
        fprintf(stderr,"%s\n",error.localizedDescription.UTF8String);return 1;
    }
    struct stat st;if(stat(file.fileSystemRepresentation,&st))return 1;
    printf("logical=%lld allocated=%lld\n",(long long)st.st_size,(long long)st.st_blocks*512);
    return 0;
}}
