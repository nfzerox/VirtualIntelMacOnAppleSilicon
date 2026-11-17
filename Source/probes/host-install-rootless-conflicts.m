// Read only: find native/reserved rootless labels on a mounted test volume.
#import <Foundation/Foundation.h>
#include <sys/xattr.h>

static NSData *Label(NSURL *url,const char *name) {
    ssize_t size=getxattr(url.fileSystemRepresentation,name,NULL,0,0,XATTR_NOFOLLOW);
    if(size<0||size>1024)return nil;
    NSMutableData *data=[NSMutableData dataWithLength:(NSUInteger)size];
    return getxattr(url.fileSystemRepresentation,name,data.mutableBytes,data.length,0,XATTR_NOFOLLOW)==size?data:nil;
}

int main(int argc,char **argv) { @autoreleasepool {
    if(argc!=2)return 64;
    NSURL *root=[NSURL fileURLWithPath:@(argv[1])];__block BOOL complete=YES;
    NSDirectoryEnumerator *files=[NSFileManager.defaultManager enumeratorAtURL:root includingPropertiesForKeys:nil options:0
        errorHandler:^BOOL(NSURL *url,NSError *error){
            fprintf(stderr,"%s: %s\n",url.fileSystemRepresentation,error.localizedDescription.UTF8String);complete=NO;return YES;
        }];
    NSUInteger count=0;
    for(NSURL *url in files) @autoreleasepool {
        NSData *reserved=Label(url,"com.apple.rootlesr"),*native=reserved?Label(url,"com.apple.rootless"):nil;
        if(!native)continue;
        printf("%s native=%s reserved=%s equal=%d\n",url.fileSystemRepresentation,
            native.description.UTF8String,reserved.description.UTF8String,[native isEqual:reserved]);count++;
    }
    printf("Rootless collisions: %lu; traversal complete: %d\n",(unsigned long)count,complete);
    return complete?0:1;
}}
