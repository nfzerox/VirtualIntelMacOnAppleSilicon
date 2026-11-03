// Read-only period Foundation path contract. No file opens or mutations.
#import <Foundation/Foundation.h>
int main(int argc,char **argv){
    if(argc!=2)return 64;NSAutoreleasePool *pool=[NSAutoreleasePool new];
    NSString *path=[NSString stringWithUTF8String:argv[1]];
    printf("input=%s\nstandardized=%s\n",[path fileSystemRepresentation],[[path stringByStandardizingPath] fileSystemRepresentation]);
    [pool drain];return 0;
}
